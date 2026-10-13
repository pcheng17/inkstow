//! UI-independent first slice of the Inkstow vault and editing session.
//! Paths sent by a UI are always vault-relative, never absolute disk paths.
mod merge;
mod move_note;
use std::fs::{self, OpenOptions};
use std::io::{self, Write};
use std::path::{Component, Path, PathBuf};

#[derive(Debug)]
pub enum Error {
    Io(io::Error),
    InvalidPath,
    MissingNote,
    Dirty,
    Conflict,
    InvalidUtf8,
    NoConflict,
    OutsideChanged,
    MergeTooLarge,
    DestinationExists,
    UnsafeLink,
    ComplexLink(String),
    UnsupportedLink(String),
    AmbiguousLink(String),
    Trash(String),
    PartialMove(String),
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Io(e) => write!(f, "{e}"),
            Self::InvalidPath => write!(f, "Expected a vault-relative Markdown path"),
            Self::MissingNote => write!(f, "Note is missing or outside the vault"),
            Self::Dirty => write!(f, "Save the current note before switching"),
            Self::Conflict => write!(f, "Note changed outside Inkstow; edits were not saved"),
            Self::InvalidUtf8 => write!(f, "Note is not valid UTF-8"),
            Self::NoConflict => write!(f, "No reported outside change to resolve"),
            Self::OutsideChanged => write!(f, "Outside note changed again; refresh before merging"),
            Self::MergeTooLarge => write!(
                f,
                "Note is too large for the line merge; edits are unchanged"
            ),
            Self::DestinationExists => write!(f, "Destination already exists"),
            Self::UnsafeLink => write!(
                f,
                "Destination cannot be represented by an extensionless wikilink"
            ),
            Self::ComplexLink(path) => {
                write!(f, "Complex wikilink syntax needs manual review in {path}")
            }
            Self::UnsupportedLink(path) => write!(
                f,
                "Unsupported or unresolved wikilink needs manual review in {path}"
            ),
            Self::AmbiguousLink(path) => write!(
                f,
                "Cannot write an unambiguous extensionless link in {path}"
            ),
            Self::Trash(message) => write!(f, "Could not move note to trash: {message}"),
            Self::PartialMove(message) => write!(
                f,
                "Move partially completed; some links may need repair: {message}"
            ),
        }
    }
}
impl std::error::Error for Error {}
impl From<io::Error> for Error {
    fn from(value: io::Error) -> Self {
        Self::Io(value)
    }
}
type Result<T> = std::result::Result<T, Error>;

fn segments(relative: &str) -> Result<Vec<&str>> {
    if relative.is_empty() || relative.contains('\\') || relative.contains('\0') {
        return Err(Error::InvalidPath);
    }
    let path = Path::new(relative);
    let parts: Vec<_> = relative.split('/').collect();
    if path.is_absolute()
        || parts
            .iter()
            .any(|p| p.is_empty() || *p == "." || *p == "..")
        || path
            .components()
            .any(|p| !matches!(p, Component::Normal(_)))
    {
        return Err(Error::InvalidPath);
    }
    Ok(parts)
}
fn markdown(relative: &str) -> Result<Vec<&str>> {
    let parts = segments(relative)?;
    if !relative.to_ascii_lowercase().ends_with(".md") {
        return Err(Error::InvalidPath);
    }
    Ok(parts)
}

#[derive(Debug)]
pub struct Vault {
    root: PathBuf,
}
impl Vault {
    pub fn open(folder: impl AsRef<Path>) -> Result<Self> {
        let root = fs::canonicalize(folder)?;
        if !root.is_dir() {
            return Err(Error::InvalidPath);
        }
        Ok(Self { root })
    }
    pub fn root(&self) -> &Path {
        &self.root
    }

    /// Ignores dot-prefixed entries and symlinks, including symlinked directories.
    pub fn list(&self) -> Result<(Vec<String>, Vec<String>)> {
        let mut notes = Vec::new();
        let mut folders = Vec::new();
        let mut pending = vec![self.root.clone()];
        while let Some(folder) = pending.pop() {
            for entry in fs::read_dir(folder)? {
                let entry = entry?;
                let name = entry.file_name();
                let Some(name) = name.to_str() else { continue };
                if name.starts_with('.') {
                    continue;
                }
                let ty = entry.file_type()?;
                if ty.is_symlink() {
                    continue;
                }
                let path = entry.path();
                let relative = path.strip_prefix(&self.root).expect("descendant");
                if ty.is_dir() {
                    folders.push(relative.to_string_lossy().replace('\\', "/"));
                    pending.push(path);
                } else if ty.is_file() && name.to_ascii_lowercase().ends_with(".md") {
                    notes.push(relative.to_string_lossy().replace('\\', "/"));
                }
            }
        }
        notes.sort();
        folders.sort();
        Ok((notes, folders))
    }

    fn checked_path(&self, relative: &str) -> Result<PathBuf> {
        let mut path = self.root.clone();
        for part in markdown(relative)? {
            path.push(part);
            if fs::symlink_metadata(&path)
                .map(|m| m.file_type().is_symlink())
                .unwrap_or(false)
            {
                return Err(Error::MissingNote);
            }
        }
        if !path.is_file() || !fs::canonicalize(&path)?.starts_with(&self.root) {
            return Err(Error::MissingNote);
        }
        Ok(path)
    }
    pub fn read(&self, relative: &str) -> Result<Vec<u8>> {
        Ok(fs::read(self.checked_path(relative)?)?)
    }
    /// Refuse to delete permanently when the OS trash is unavailable.
    pub fn trash(&self, relative: &str) -> Result<()> {
        self.trash_with(relative, |path| {
            trash::delete(path).map_err(|e| Error::Trash(e.to_string()))
        })
    }
    pub fn trash_with(
        &self,
        relative: &str,
        trash: impl FnOnce(&Path) -> Result<()>,
    ) -> Result<()> {
        trash(&self.checked_path(relative)?)
    }
    /// Compare disk bytes before staging and again before replacement. Like the Qt
    /// implementation, this cannot exclude an external write between check and rename.
    pub fn save(&self, relative: &str, expected: &[u8], replacement: &[u8]) -> Result<()> {
        let path = self.checked_path(relative)?;
        if fs::read(&path)? != expected {
            return Err(Error::Conflict);
        }
        let parent = path.parent().expect("note has a parent");
        let mut staged = tempfile::NamedTempFile::new_in(parent)?;
        staged
            .as_file()
            .set_permissions(fs::metadata(&path)?.permissions())?;
        staged.write_all(replacement)?;
        staged.as_file().sync_all()?;
        if self.checked_path(relative)? != path || fs::read(&path)? != expected {
            return Err(Error::Conflict);
        }
        staged.persist(&path).map_err(|e| Error::Io(e.error))?;
        Ok(())
    }
    pub fn create(&self, relative: &str, bytes: &[u8]) -> Result<()> {
        let parts = markdown(relative)?;
        if parts.iter().any(|p| p.starts_with('.')) {
            return Err(Error::InvalidPath);
        }
        let mut path = self.root.clone();
        for part in &parts[..parts.len() - 1] {
            path.push(part);
            match fs::symlink_metadata(&path) {
                Ok(m) if m.file_type().is_symlink() || !m.is_dir() => {
                    return Err(Error::InvalidPath)
                }
                Ok(_) => (),
                Err(e) if e.kind() == io::ErrorKind::NotFound => fs::create_dir(&path)?,
                Err(e) => return Err(e.into()),
            }
        }
        path.push(parts.last().expect("nonempty"));
        let mut file = OpenOptions::new().write(true).create_new(true).open(path)?;
        file.write_all(bytes)?;
        Ok(())
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DiskChange {
    None,
    Reloaded,
    Conflict,
    Missing,
}

fn decode(bytes: &[u8]) -> Result<(String, bool)> {
    let bom = bytes.starts_with(&[0xef, 0xbb, 0xbf]);
    let text = std::str::from_utf8(if bom { &bytes[3..] } else { bytes })
        .map_err(|_| Error::InvalidUtf8)?
        .to_owned();
    Ok((text, bom))
}

#[derive(Debug, Default)]
pub struct Session {
    vault: Option<Vault>,
    path: Option<String>,
    content: String,
    saved_content: String,
    saved_bytes: Vec<u8>,
    conflict_bytes: Option<Vec<u8>>,
    bom: bool,
}
impl Session {
    pub fn new() -> Self {
        Self::default()
    }
    pub fn dirty(&self) -> bool {
        self.content != self.saved_content
    }
    pub fn vault(&self) -> Option<&Vault> {
        self.vault.as_ref()
    }
    pub fn path(&self) -> Option<&str> {
        self.path.as_deref()
    }
    pub fn content(&self) -> &str {
        &self.content
    }
    pub fn set_content(&mut self, text: String) {
        self.content = text;
    }
    pub fn open_vault(&mut self, folder: impl AsRef<Path>) -> Result<()> {
        if self.dirty() {
            return Err(Error::Dirty);
        }
        let vault = Vault::open(folder)?;
        vault.list()?;
        self.vault = Some(vault);
        self.path = None;
        self.content.clear();
        self.saved_content.clear();
        self.saved_bytes.clear();
        self.conflict_bytes = None;
        self.bom = false;
        Ok(())
    }
    pub fn select(&mut self, path: &str) -> Result<()> {
        if self.dirty() {
            return Err(Error::Dirty);
        }
        let vault = self.vault.as_ref().ok_or(Error::MissingNote)?;
        if !vault.list()?.0.iter().any(|p| p == path) {
            return Err(Error::MissingNote);
        }
        let bytes = vault.read(path)?;
        let (text, _) = decode(&bytes)?;
        self.load(path, bytes, text);
        Ok(())
    }
    fn encoded(&self) -> Vec<u8> {
        let mut bytes = if self.bom {
            vec![0xef, 0xbb, 0xbf]
        } else {
            vec![]
        };
        bytes.extend_from_slice(self.content.as_bytes());
        bytes
    }
    fn write(&mut self, expected: &[u8]) -> Result<()> {
        let vault = self.vault.as_ref().ok_or(Error::MissingNote)?;
        let path = self.path.as_deref().ok_or(Error::MissingNote)?;
        let bytes = self.encoded();
        vault.save(path, expected, &bytes)?;
        self.saved_bytes = bytes;
        self.saved_content = self.content.clone();
        self.conflict_bytes = None;
        Ok(())
    }
    pub fn save(&mut self) -> Result<()> {
        let expected = self.saved_bytes.clone();
        self.write(&expected)
    }
    pub fn sync_with_disk(&mut self) -> DiskChange {
        let (Some(vault), Some(path)) = (&self.vault, &self.path) else {
            return DiskChange::None;
        };
        let Ok(bytes) = vault.read(path) else {
            return DiskChange::Missing;
        };
        if bytes == self.saved_bytes {
            self.conflict_bytes = None;
            return DiskChange::None;
        }
        self.conflict_bytes = Some(bytes.clone());
        if self.dirty() {
            return DiskChange::Conflict;
        }
        let Ok((text, _)) = decode(&bytes) else {
            return DiskChange::Conflict;
        };
        let path = path.clone();
        self.load(&path, bytes, text);
        DiskChange::Reloaded
    }
    pub fn reload_from_disk(&mut self) -> Result<()> {
        let vault = self.vault.as_ref().ok_or(Error::MissingNote)?;
        let path = self.path.as_deref().ok_or(Error::MissingNote)?;
        let bytes = vault.read(path)?;
        let (text, _) = decode(&bytes)?;
        let path = path.to_owned();
        self.load(&path, bytes, text);
        Ok(())
    }
    pub fn overwrite_disk(&mut self) -> Result<()> {
        let expected = self.conflict_bytes.clone().ok_or(Error::NoConflict)?;
        self.write(&expected)
    }
    pub fn merge_outside_changes(&mut self) -> Result<()> {
        let expected = self.conflict_bytes.as_ref().ok_or(Error::NoConflict)?;
        if !self.dirty() {
            return Err(Error::NoConflict);
        }
        let vault = self.vault.as_ref().ok_or(Error::MissingNote)?;
        let path = self.path.as_deref().ok_or(Error::MissingNote)?;
        let current = vault.read(path)?;
        let (outside, bom) = decode(&current)?;
        if &current != expected {
            return Err(Error::OutsideChanged);
        }
        let merged = merge::three_way(&self.saved_content, &self.content, &outside)
            .ok_or(Error::MergeTooLarge)?;
        self.saved_bytes = current;
        self.saved_content = outside;
        self.bom = bom;
        self.content = merged;
        self.conflict_bytes = None;
        Ok(())
    }
    pub fn save_as_new_note(&mut self) -> Result<()> {
        let vault = self.vault.as_ref().ok_or(Error::MissingNote)?;
        let path = self.path.as_deref().ok_or(Error::MissingNote)?;
        let bytes = self.encoded();
        vault.create(path, &bytes)?;
        self.saved_bytes = bytes;
        self.saved_content = self.content.clone();
        self.conflict_bytes = None;
        Ok(())
    }
    pub fn discard_edits(&mut self) {
        self.content = self.saved_content.clone();
        self.conflict_bytes = None;
    }
    fn load(&mut self, path: &str, bytes: Vec<u8>, text: String) {
        self.bom = bytes.starts_with(&[0xef, 0xbb, 0xbf]);
        self.path = Some(path.to_owned());
        self.saved_bytes = bytes;
        self.content = text.clone();
        self.saved_content = text;
        self.conflict_bytes = None;
    }
    pub fn trash_note(&mut self) -> Result<()> {
        self.trash_with(|path| trash::delete(path).map_err(|e| Error::Trash(e.to_string())))
    }
    pub fn trash_with(&mut self, trash: impl FnOnce(&Path) -> Result<()>) -> Result<()> {
        if self.dirty() {
            return Err(Error::Dirty);
        }
        let vault = self.vault.as_ref().ok_or(Error::MissingNote)?;
        let path = self.path.as_deref().ok_or(Error::MissingNote)?;
        vault.trash_with(path, trash)?;
        self.path = None;
        self.content.clear();
        self.saved_content.clear();
        self.saved_bytes.clear();
        self.conflict_bytes = None;
        self.bom = false;
        Ok(())
    }
    pub fn move_note(&mut self, input: &str, update_links: bool) -> Result<()> {
        if self.dirty() {
            return Err(Error::Dirty);
        }
        let vault = self.vault.as_ref().ok_or(Error::MissingNote)?;
        let from = self.path.as_deref().ok_or(Error::MissingNote)?.to_owned();
        let input = input.trim();
        let to = if input.to_ascii_lowercase().ends_with(".md") {
            input.to_owned()
        } else {
            format!("{input}.md")
        };
        let result = vault.move_note(&from, &to, &self.saved_bytes, update_links);
        if result.is_ok() || matches!(result, Err(Error::PartialMove(_))) {
            self.path = Some(to.clone());
            if let Err(error) = self.reload_from_disk() {
                return Err(Error::PartialMove(error.to_string()));
            }
        }
        result
    }
    pub fn create(&mut self, input: &str) -> Result<()> {
        if self.dirty() {
            return Err(Error::Dirty);
        }
        let vault = self.vault.as_ref().ok_or(Error::MissingNote)?;
        let name = if input.to_ascii_lowercase().ends_with(".md") {
            input.to_owned()
        } else {
            format!("{input}.md")
        };
        vault.create(&name, b"")?;
        self.select(&name)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn discovery_and_edit_conflicts() {
        let dir = tempfile::tempdir().unwrap();
        fs::create_dir(dir.path().join("Folder")).unwrap();
        fs::write(dir.path().join("Folder/Note.md"), b"\xef\xbb\xbfhello").unwrap();
        fs::write(dir.path().join(".hidden.md"), b"hidden").unwrap();
        let mut session = Session::new();
        session.open_vault(dir.path()).unwrap();
        assert_eq!(
            session.vault().unwrap().list().unwrap().0,
            ["Folder/Note.md"]
        );
        session.select("Folder/Note.md").unwrap();
        session.set_content("mine".into());
        assert!(matches!(
            session.select("Folder/Note.md"),
            Err(Error::Dirty)
        ));
        fs::write(dir.path().join("Folder/Note.md"), b"outside").unwrap();
        assert!(matches!(session.save(), Err(Error::Conflict)));
        assert_eq!(
            fs::read(dir.path().join("Folder/Note.md")).unwrap(),
            b"outside"
        );
        assert!(session.dirty());
    }
    #[test]
    fn create_and_preserve_bom() {
        let dir = tempfile::tempdir().unwrap();
        let mut session = Session::new();
        session.open_vault(dir.path()).unwrap();
        session.create("New/Note").unwrap();
        session.set_content("text".into());
        session.save().unwrap();
        assert_eq!(fs::read(dir.path().join("New/Note.md")).unwrap(), b"text");
        fs::write(dir.path().join("New/Note.md"), b"\xef\xbb\xbftext").unwrap();
        session.select("New/Note.md").unwrap();
        session.set_content("updated".into());
        session.save().unwrap();
        assert_eq!(
            fs::read(dir.path().join("New/Note.md")).unwrap(),
            b"\xef\xbb\xbfupdated"
        );
        assert!(matches!(
            session.create("../escape"),
            Err(Error::InvalidPath)
        ));
    }
    #[test]
    fn clean_change_reloads_but_invalid_utf8_does_not_destroy_editor_text() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("Note.md");
        fs::write(&path, b"original").unwrap();
        let mut s = Session::new();
        s.open_vault(dir.path()).unwrap();
        s.select("Note.md").unwrap();
        fs::write(&path, b"updated").unwrap();
        assert_eq!(s.sync_with_disk(), DiskChange::Reloaded);
        assert_eq!(s.content(), "updated");
        assert!(!s.dirty());
        fs::write(&path, b"\xff").unwrap();
        assert_eq!(s.sync_with_disk(), DiskChange::Conflict);
        assert_eq!(s.content(), "updated");
        assert!(matches!(s.reload_from_disk(), Err(Error::InvalidUtf8)));
    }
    #[test]
    fn keep_mine_only_replaces_reported_version() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("Note.md");
        fs::write(&path, b"base").unwrap();
        let mut s = Session::new();
        s.open_vault(dir.path()).unwrap();
        s.select("Note.md").unwrap();
        s.set_content("mine".into());
        fs::write(&path, b"outside").unwrap();
        assert_eq!(s.sync_with_disk(), DiskChange::Conflict);
        fs::write(&path, b"newer").unwrap();
        assert!(matches!(s.overwrite_disk(), Err(Error::Conflict)));
        assert_eq!(fs::read(&path).unwrap(), b"newer");
        assert_eq!(s.sync_with_disk(), DiskChange::Conflict);
        s.overwrite_disk().unwrap();
        assert_eq!(fs::read(&path).unwrap(), b"mine");
        assert!(!s.dirty());
    }
    #[test]
    fn merge_is_unsaved_and_against_latest_reported_disk_version() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("Note.md");
        fs::write(&path, b"a\nb\nc").unwrap();
        let mut s = Session::new();
        s.open_vault(dir.path()).unwrap();
        s.select("Note.md").unwrap();
        s.set_content("A\nb\nc".into());
        fs::write(&path, b"a\nb\nC").unwrap();
        assert_eq!(s.sync_with_disk(), DiskChange::Conflict);
        fs::write(&path, b"newer").unwrap();
        assert!(matches!(
            s.merge_outside_changes(),
            Err(Error::OutsideChanged)
        ));
        assert_eq!(s.content(), "A\nb\nc");
        fs::write(&path, b"a\nb\nC").unwrap();
        s.merge_outside_changes().unwrap();
        assert_eq!(s.content(), "A\nb\nC");
        assert_eq!(fs::read(&path).unwrap(), b"a\nb\nC");
        assert!(s.dirty());
        s.save().unwrap();
        assert_eq!(fs::read(&path).unwrap(), b"A\nb\nC");
    }
    #[test]
    fn missing_note_can_be_recreated_without_replacing_reappearance() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("Note.md");
        fs::write(&path, b"base").unwrap();
        let mut s = Session::new();
        s.open_vault(dir.path()).unwrap();
        s.select("Note.md").unwrap();
        s.set_content("mine".into());
        fs::remove_file(&path).unwrap();
        assert_eq!(s.sync_with_disk(), DiskChange::Missing);
        fs::write(&path, b"reappeared").unwrap();
        assert!(s.save_as_new_note().is_err());
        assert_eq!(fs::read(&path).unwrap(), b"reappeared");
        fs::remove_file(&path).unwrap();
        s.save_as_new_note().unwrap();
        assert_eq!(fs::read(&path).unwrap(), b"mine");
    }
    #[test]
    fn reverted_change_clears_conflict_and_reload_discards_edits() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("Note.md");
        fs::write(&path, b"base").unwrap();
        let mut s = Session::new();
        s.open_vault(dir.path()).unwrap();
        s.select("Note.md").unwrap();
        s.set_content("mine".into());
        fs::write(&path, b"outside").unwrap();
        assert_eq!(s.sync_with_disk(), DiskChange::Conflict);
        fs::write(&path, b"base").unwrap();
        assert_eq!(s.sync_with_disk(), DiskChange::None);
        assert!(matches!(s.overwrite_disk(), Err(Error::NoConflict)));
        fs::write(&path, b"outside").unwrap();
        s.reload_from_disk().unwrap();
        assert_eq!(s.content(), "outside");
        assert!(!s.dirty());
    }
    #[test]
    fn trash_refuses_dirty_or_failed_backend_without_removing_file() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("Note.md");
        fs::write(&path, b"note").unwrap();
        let mut s = Session::new();
        s.open_vault(dir.path()).unwrap();
        s.select("Note.md").unwrap();
        s.set_content("unsaved".into());
        assert!(matches!(
            s.trash_with(|_| panic!("should not trash dirty note")),
            Err(Error::Dirty)
        ));
        s.discard_edits();
        assert!(s
            .trash_with(|_| Err(Error::Trash("unavailable".into())))
            .is_err());
        assert_eq!(s.path(), Some("Note.md"));
        assert!(path.exists());
        s.trash_with(|path| {
            fs::remove_file(path)?;
            Ok(())
        })
        .unwrap();
        assert_eq!(s.path(), None);
        assert!(!path.exists());
    }
    #[test]
    fn rename_updates_unique_fixture_links_without_touching_original() {
        let dir = tempfile::tempdir().unwrap();
        let fixture = Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../../tests/fixtures/obsidian-rename/unique");
        fs::create_dir(dir.path().join("subdir")).unwrap();
        for note in ["SourceBare.md", "SourceQualified.md", "subdir/Target.md"] {
            fs::copy(fixture.join(note), dir.path().join(note)).unwrap();
        }
        let mut s = Session::new();
        s.open_vault(dir.path()).unwrap();
        s.select("subdir/Target.md").unwrap();
        s.move_note("subdir/New", true).unwrap();
        assert_eq!(s.path(), Some("subdir/New.md"));
        assert!(!s.dirty());
        assert!(dir.path().join("subdir/New.md").exists());
        assert!(!dir.path().join("subdir/Target.md").exists());
        for note in ["SourceBare.md", "SourceQualified.md"] {
            let bytes = fs::read_to_string(dir.path().join(note)).unwrap();
            assert!(bytes.contains("[[/subdir/New]]"), "{bytes}");
        }
    }
    #[test]
    fn duplicate_targets_and_bom_rewrite_without_breaking_other_links() {
        let dir = tempfile::tempdir().unwrap();
        for folder in ["A", "B", "C"] {
            fs::create_dir(dir.path().join(folder)).unwrap();
        }
        fs::write(dir.path().join("A/Target.md"), b"\xef\xbb\xbf[[Target]]\n").unwrap();
        fs::write(dir.path().join("B/Target.md"), b"other").unwrap();
        fs::write(
            dir.path().join("A/Source.md"),
            b"[[Target]] [[B/Target.md]]",
        )
        .unwrap();
        fs::write(dir.path().join("B/Source.md"), b"[[Target]]").unwrap();
        let mut s = Session::new();
        s.open_vault(dir.path()).unwrap();
        s.select("A/Target.md").unwrap();
        s.move_note("C/Target", true).unwrap();
        assert_eq!(
            fs::read(dir.path().join("C/Target.md")).unwrap(),
            b"\xef\xbb\xbf[[/C/Target]]\n"
        );
        assert_eq!(
            fs::read(dir.path().join("A/Source.md")).unwrap(),
            b"[[/C/Target]] [[/B/Target]]"
        );
        assert_eq!(
            fs::read(dir.path().join("B/Source.md")).unwrap(),
            b"[[/B/Target]]"
        );
        assert_eq!(s.path(), Some("C/Target.md"));
        assert_eq!(s.content(), "[[/C/Target]]\n");
    }
    #[test]
    fn rename_preflights_unsafe_links_shadowing_and_external_writes() {
        let dir = tempfile::tempdir().unwrap();
        let target = dir.path().join("Old.md");
        let backlink = dir.path().join("Source.md");
        fs::write(&target, b"original").unwrap();
        fs::write(&backlink, b"[[Old]]").unwrap();
        let mut s = Session::new();
        s.open_vault(dir.path()).unwrap();
        s.select("Old.md").unwrap();
        for name in [
            "New#Part",
            "New|Alias",
            "New[Part]",
            "New.md.md",
            "../escape",
            "missing/New",
        ] {
            assert!(s.move_note(name, true).is_err(), "{name}");
        }
        fs::write(dir.path().join("New"), b"attachment").unwrap();
        assert!(s.move_note("New", true).is_err());
        fs::remove_file(dir.path().join("New")).unwrap();
        fs::write(&backlink, b"[[Old|alias]]").unwrap();
        assert!(s.move_note("New", true).is_err());
        assert_eq!(fs::read(&target).unwrap(), b"original");
        assert!(!dir.path().join("New.md").exists());
        s.move_note("New", false).unwrap();
        assert_eq!(fs::read(&backlink).unwrap(), b"[[Old|alias]]");
        fs::write(dir.path().join("Outside.md"), b"before").unwrap();
        s.select("Outside.md").unwrap();
        fs::write(dir.path().join("Outside.md"), b"outside").unwrap();
        assert!(matches!(s.move_note("Later", false), Err(Error::Conflict)));
        assert!(dir.path().join("Outside.md").exists());
    }
    #[test]
    fn rename_rejects_unresolved_relevant_link_and_preserves_files() {
        let dir = tempfile::tempdir().unwrap();
        fs::write(dir.path().join("Old.md"), b"note").unwrap();
        fs::write(dir.path().join("Source.md"), b"[[New]]").unwrap();
        let mut s = Session::new();
        s.open_vault(dir.path()).unwrap();
        s.select("Old.md").unwrap();
        assert!(matches!(
            s.move_note("New", true),
            Err(Error::UnsupportedLink(_))
        ));
        assert_eq!(fs::read(dir.path().join("Old.md")).unwrap(), b"note");
        assert!(!dir.path().join("New.md").exists());
        assert_eq!(fs::read(dir.path().join("Source.md")).unwrap(), b"[[New]]");
    }
    #[cfg(unix)]
    #[test]
    fn rename_and_trash_refuse_symlinked_targets() {
        use std::os::unix::fs::symlink;
        let dir = tempfile::tempdir().unwrap();
        fs::write(dir.path().join("Old.md"), b"note").unwrap();
        symlink(dir.path().join("Old.md"), dir.path().join("Linked.md")).unwrap();
        fs::create_dir(dir.path().join("Folder")).unwrap();
        symlink(dir.path().join("Folder"), dir.path().join("LinkedFolder")).unwrap();
        let mut s = Session::new();
        s.open_vault(dir.path()).unwrap();
        s.select("Old.md").unwrap();
        assert!(s.move_note("Linked", false).is_err());
        assert!(s.move_note("LinkedFolder/New", false).is_err());
        assert!(s
            .vault()
            .unwrap()
            .trash_with("Linked.md", |_| panic!("followed link"))
            .is_err());
        assert_eq!(fs::read(dir.path().join("Old.md")).unwrap(), b"note");
    }
    #[test]
    fn sample_fixture_is_discovered_without_modifying_it() {
        let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/sample");
        let vault = Vault::open(fixture).unwrap();
        assert_eq!(
            vault.list().unwrap().0,
            ["Folder/Second note.md", "Welcome.md"]
        );
        assert!(matches!(
            vault.read("../Welcome.md"),
            Err(Error::InvalidPath)
        ));
    }
    #[cfg(unix)]
    #[test]
    fn symlinks_are_never_traversed() {
        use std::os::unix::fs::symlink;
        let dir = tempfile::tempdir().unwrap();
        let outside = tempfile::tempdir().unwrap();
        fs::write(outside.path().join("secret.md"), b"secret").unwrap();
        symlink(outside.path(), dir.path().join("linked")).unwrap();
        let vault = Vault::open(dir.path()).unwrap();
        assert!(vault.list().unwrap().0.is_empty());
        assert!(vault.read("linked/secret.md").is_err());
        assert!(vault.create("linked/new.md", b"").is_err());
    }
}
