//! Filesystem-only vault operations. No UI or webview dependency.
use std::fs;
use std::io;
use std::path::{Component, Path, PathBuf};

#[derive(Debug)]
pub struct Vault {
    root: PathBuf,
}

impl Vault {
    pub fn open(path: impl AsRef<Path>) -> io::Result<Self> {
        let root = path.as_ref().canonicalize()?;
        if !root.is_dir() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "vault must be a directory",
            ));
        }
        Ok(Self { root })
    }

    /// Discover notes; paths are relative to the vault and use forward slashes.
    /// Directory symlinks are not traversed to avoid cycles and escaped roots.
    pub fn list_notes(&self) -> io::Result<Vec<String>> {
        let mut notes = Vec::new();
        let mut pending = vec![self.root.clone()];
        while let Some(dir) = pending.pop() {
            for entry in fs::read_dir(dir)? {
                let entry = entry?;
                let kind = entry.file_type()?;
                let path = entry.path();
                if kind.is_dir() {
                    if entry.file_name() != ".obsidian" {
                        pending.push(path);
                    }
                } else if kind.is_file()
                    && path
                        .extension()
                        .is_some_and(|ext| ext.eq_ignore_ascii_case("md"))
                {
                    let relative = path
                        .strip_prefix(&self.root)
                        .expect("scanned path belongs to root");
                    notes.push(
                        relative
                            .to_string_lossy()
                            .replace(std::path::MAIN_SEPARATOR, "/"),
                    );
                }
            }
        }
        notes.sort();
        Ok(notes)
    }

    pub fn read_note(&self, relative: &str) -> io::Result<String> {
        fs::read_to_string(self.note_path(relative)?)
    }

    /// Only existing Markdown files within this vault may be written.
    pub fn save_note(&self, relative: &str, content: &str) -> io::Result<()> {
        fs::write(self.note_path(relative)?, content)
    }

    fn note_path(&self, relative: &str) -> io::Result<PathBuf> {
        let path = Path::new(relative);
        if path.components().next().is_none()
            || !path
                .components()
                .all(|part| matches!(part, Component::Normal(_)))
            || path
                .extension()
                .is_none_or(|ext| !ext.eq_ignore_ascii_case("md"))
        {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "expected a relative Markdown path",
            ));
        }
        let resolved = self.root.join(path).canonicalize()?;
        if !resolved.starts_with(&self.root)
            || !resolved.is_file()
            || resolved.starts_with(self.root.join(".obsidian"))
        {
            return Err(io::Error::new(
                io::ErrorKind::PermissionDenied,
                "note is outside the vault",
            ));
        }
        Ok(resolved)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::tempdir;

    #[test]
    fn lists_and_round_trips_markdown_without_touching_configuration() {
        let dir = tempdir().unwrap();
        fs::create_dir_all(dir.path().join("folder")).unwrap();
        fs::create_dir(dir.path().join(".obsidian")).unwrap();
        fs::write(
            dir.path().join("folder/note.md"),
            "---\ntags: [draft]\n---\n[[Other]]\n",
        )
        .unwrap();
        fs::write(dir.path().join(".obsidian/private.md"), "config").unwrap();
        fs::write(dir.path().join("photo.png"), "binary").unwrap();
        let vault = Vault::open(dir.path()).unwrap();
        assert_eq!(vault.list_notes().unwrap(), ["folder/note.md"]);
        let original = vault.read_note("folder/note.md").unwrap();
        vault.save_note("folder/note.md", &original).unwrap();
        assert_eq!(
            fs::read_to_string(dir.path().join("folder/note.md")).unwrap(),
            original
        );
        assert!(vault.read_note(".obsidian/private.md").is_err());
    }

    #[test]
    fn rejects_escape_and_non_markdown_paths() {
        let dir = tempdir().unwrap();
        fs::write(dir.path().join("note.md"), "safe").unwrap();
        let vault = Vault::open(dir.path()).unwrap();
        for path in ["../note.md", "/tmp/note.md", "note.md/..", "", "photo.png"] {
            assert!(vault.save_note(path, "unsafe").is_err(), "accepted {path}");
        }
        assert_eq!(vault.read_note("note.md").unwrap(), "safe");
    }

    #[cfg(unix)]
    #[test]
    fn ignores_symlinked_directories_and_blocks_symlinked_files_outside_vault() {
        use std::os::unix::fs::symlink;
        let dir = tempdir().unwrap();
        let other = tempdir().unwrap();
        fs::write(other.path().join("secret.md"), "outside").unwrap();
        symlink(other.path(), dir.path().join("external")).unwrap();
        symlink(
            other.path().join("secret.md"),
            dir.path().join("shortcut.md"),
        )
        .unwrap();
        let vault = Vault::open(dir.path()).unwrap();
        assert!(vault.list_notes().unwrap().is_empty());
        assert!(vault.read_note("shortcut.md").is_err());
        assert!(vault.save_note("external/secret.md", "bad").is_err());
        assert_eq!(
            fs::read_to_string(other.path().join("secret.md")).unwrap(),
            "outside"
        );
    }
}
