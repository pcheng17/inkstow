//! Filesystem-only vault operations. No UI or webview dependency.
use std::fs;
use std::io::{self, Write};
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
    /// Dot-prefixed entries and directory symlinks are not traversed.
    pub fn list_notes(&self) -> io::Result<Vec<String>> {
        let mut notes = Vec::new();
        let mut pending = vec![self.root.clone()];
        while let Some(dir) = pending.pop() {
            for entry in fs::read_dir(dir)? {
                let entry = entry?;
                if entry.file_name().to_string_lossy().starts_with('.') {
                    continue;
                }
                let kind = entry.file_type()?;
                let path = entry.path();
                if kind.is_dir() {
                    pending.push(path);
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

    /// Replace an existing note atomically only if its content still matches the last read.
    /// This detects external changes, but cannot prevent an uncooperative writer from
    /// changing the file between the final check and rename.
    pub fn save_note(&self, relative: &str, expected: &str, content: &str) -> io::Result<()> {
        let path = self.note_path(relative)?;
        let current = fs::read_to_string(&path)?;
        if current != expected {
            return Err(save_conflict());
        }
        let parent = path.parent().expect("note has a parent directory");
        let mut replacement = tempfile::NamedTempFile::new_in(parent)?;
        replacement
            .as_file()
            .set_permissions(fs::metadata(&path)?.permissions())?;
        replacement.write_all(content.as_bytes())?;
        replacement.as_file().sync_all()?;
        // Recheck after preparing the replacement to narrow the external-edit race.
        if self.note_path(relative)? != path || fs::read_to_string(&path)? != expected {
            return Err(save_conflict());
        }
        replacement.persist(&path).map_err(|error| error.error)?;
        Ok(())
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

fn save_conflict() -> io::Error {
    io::Error::other("Save conflict: note changed on disk. Your edits are still in the editor; copy them before reopening the note.")
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
        vault
            .save_note("folder/note.md", &original, &original)
            .unwrap();
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
            assert!(
                vault.save_note(path, "safe", "unsafe").is_err(),
                "accepted {path}"
            );
        }
        assert_eq!(vault.read_note("note.md").unwrap(), "safe");
    }

    #[test]
    fn refuses_to_overwrite_an_externally_changed_or_deleted_note() {
        let dir = tempdir().unwrap();
        let path = dir.path().join("note.md");
        fs::write(&path, "original").unwrap();
        let vault = Vault::open(dir.path()).unwrap();
        let expected = vault.read_note("note.md").unwrap();
        fs::write(&path, "external edit").unwrap();
        assert!(vault
            .save_note("note.md", &expected, "my edit")
            .unwrap_err()
            .to_string()
            .contains("Save conflict"));
        assert_eq!(fs::read_to_string(&path).unwrap(), "external edit");
        fs::remove_file(&path).unwrap();
        assert!(vault.save_note("note.md", &expected, "my edit").is_err());
        assert!(!path.exists());
    }

    #[cfg(unix)]
    #[test]
    fn atomic_save_preserves_permissions_and_replaces_the_file() {
        use std::os::unix::fs::{MetadataExt, PermissionsExt};
        let dir = tempdir().unwrap();
        let path = dir.path().join("note.md");
        fs::write(&path, "old").unwrap();
        fs::set_permissions(&path, fs::Permissions::from_mode(0o640)).unwrap();
        let old_inode = fs::metadata(&path).unwrap().ino();
        let vault = Vault::open(dir.path()).unwrap();
        vault.save_note("note.md", "old", "new").unwrap();
        assert_eq!(fs::read_to_string(&path).unwrap(), "new");
        assert_ne!(fs::metadata(&path).unwrap().ino(), old_inode);
        assert_eq!(
            fs::metadata(&path).unwrap().permissions().mode() & 0o777,
            0o640
        );
        assert_eq!(fs::read_dir(dir.path()).unwrap().count(), 1);
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
        assert!(vault
            .save_note("external/secret.md", "outside", "bad")
            .is_err());
        assert_eq!(
            fs::read_to_string(other.path().join("secret.md")).unwrap(),
            "outside"
        );
    }
}
