//! Wikilink resolution shared by note moves and link following, plus attachment
//! revalidation. Remaining Obsidian differences: targets follow static inspection
//! of Obsidian, not live observation; headings and aliases are stripped and heading
//! navigation is not implemented; duplicate-name tie breaks, Unicode case folding
//! and path ordering are unverified; `![[embeds]]` are treated like plain links.
use crate::{Error, Result, Vault};
use regex::Regex;
use std::fs;
use std::path::{Path, PathBuf};

/// `[[...]]` wikilinks on one line; capture 1 is the link text.
pub(crate) fn link_regex() -> Regex {
    Regex::new(r"\[\[([^\[\]\n]+)\]\]").expect("static regex")
}

/// Link text of the `[[...]]` containing `caret` (a UTF-16 offset, as the web
/// editor reports it), using the same half-open range as the Qt editor.
pub fn link_at(text: &str, caret: usize) -> Option<&str> {
    let mut units = 0;
    let mut position = text.len();
    for (index, c) in text.char_indices() {
        if units >= caret {
            position = index;
            break;
        }
        units += c.len_utf16();
    }
    let found = link_regex().captures_iter(text).find(|capture| {
        let full = capture.get(0).expect("full match");
        (full.start()..full.end()).contains(&position)
    })?;
    Some(found.get(1).expect("link capture").as_str())
}

pub(crate) fn basename(path: &str) -> &str {
    path.rsplit('/').next().unwrap_or(path)
}
pub(crate) fn eq(a: &str, b: &str) -> bool {
    a.to_lowercase() == b.to_lowercase()
}

// Share one discovery view across every backlink, including attachments and files
// without extensions (which can shadow extensionless Markdown links).
pub(crate) fn visible_files(root: &Path) -> Result<Vec<String>> {
    let mut files = Vec::new();
    let mut pending = vec![root.to_path_buf()];
    while let Some(folder) = pending.pop() {
        for entry in fs::read_dir(folder)? {
            let entry = entry?;
            let name = entry.file_name();
            let Some(name) = name.to_str() else {
                continue;
            };
            if name.starts_with('.') {
                continue;
            }
            let ty = entry.file_type()?;
            if ty.is_symlink() {
                continue;
            }
            let path = entry.path();
            if ty.is_dir() {
                pending.push(path);
            } else if ty.is_file() {
                files.push(
                    path.strip_prefix(root)
                        .expect("descendant")
                        .to_string_lossy()
                        .replace('\\', "/"),
                );
            }
        }
    }
    files.sort();
    Ok(files)
}
pub(crate) fn resolve(link: &str, source: &str, files: &[String]) -> Option<String> {
    let path = link.split('|').next()?.split('#').next()?;
    let root_only = path.starts_with('/');
    let path = if root_only { &path[1..] } else { path };
    if path.is_empty()
        || path.contains('\\')
        || path.split('/').any(|p| p.is_empty() || p.starts_with('.'))
    {
        return None;
    }
    let spellings = if path.to_ascii_lowercase().ends_with(".md") {
        vec![path.to_owned()]
    } else {
        vec![path.to_owned(), format!("{path}.md")]
    };
    let source_folder = source.rsplit_once('/').map_or("", |(folder, _)| folder);
    let nearby = |spelling: &str| {
        if source_folder.is_empty() {
            spelling.to_owned()
        } else {
            format!("{source_folder}/{spelling}")
        }
    };
    for spelling in &spellings {
        if !root_only && files.contains(&nearby(spelling)) {
            return Some(nearby(spelling));
        }
        if files.contains(spelling) {
            return Some(spelling.clone());
        }
    }
    if path.contains('/') {
        for spelling in &spellings {
            if !root_only {
                if let Some(file) = files.iter().find(|f| eq(f, &nearby(spelling))) {
                    return Some(file.clone());
                }
            }
            if let Some(file) = files.iter().find(|f| eq(f, spelling)) {
                return Some(file.clone());
            }
        }
        return None;
    }
    if root_only {
        return None;
    }
    for spelling in &spellings {
        let mut matches: Vec<_> = files
            .iter()
            .filter(|file| eq(basename(file), spelling))
            .collect();
        matches.sort_by_key(|file| {
            let near = source_folder.is_empty() || file.starts_with(&format!("{source_folder}/"));
            (!near, file.chars().count(), *file)
        });
        if let Some(file) = matches.first() {
            return Some((*file).clone());
        }
    }
    None
}

impl Vault {
    /// Vault-relative file (note or attachment) a link in `source` points to.
    pub fn resolve_link(&self, link: &str, source: &str) -> Result<Option<String>> {
        Ok(resolve(link, source, &visible_files(self.root())?))
    }
    /// Revalidate a vault-relative attachment before it is handed to an external
    /// opener: no dot segments, symlinks, Markdown notes or escapes from the vault.
    pub fn attachment_path(&self, relative: &str) -> Result<PathBuf> {
        if relative.is_empty()
            || relative.contains(['\\', '\0'])
            || relative.to_ascii_lowercase().ends_with(".md")
        {
            return Err(Error::InvalidPath);
        }
        let mut path = self.root().to_path_buf();
        for part in relative.split('/') {
            if part.is_empty() || part.starts_with('.') {
                return Err(Error::InvalidPath);
            }
            path.push(part);
            if fs::symlink_metadata(&path)
                .map_err(|_| Error::MissingNote)?
                .file_type()
                .is_symlink()
            {
                return Err(Error::MissingNote);
            }
        }
        let canonical = fs::canonicalize(&path).map_err(|_| Error::MissingNote)?;
        if !canonical.is_file() || !canonical.starts_with(self.root()) {
            return Err(Error::MissingNote);
        }
        Ok(canonical)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{Followed, Session};

    fn copy(from: &Path, to: &Path) {
        fs::create_dir_all(to).unwrap();
        for entry in fs::read_dir(from).unwrap() {
            let entry = entry.unwrap();
            let target = to.join(entry.file_name());
            if entry.file_type().unwrap().is_dir() {
                copy(&entry.path(), &target);
            } else {
                fs::copy(entry.path(), target).unwrap();
            }
        }
    }
    fn vault() -> (tempfile::TempDir, Vault) {
        let dir = tempfile::tempdir().unwrap();
        let fixture = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/links");
        copy(&fixture, dir.path());
        let vault = Vault::open(dir.path()).unwrap();
        (dir, vault)
    }

    #[test]
    fn fixture_links_resolve_like_the_readme() {
        let (_dir, vault) = vault();
        let cases = [
            ("Twin", Some("A/Twin.md")),
            ("B/Twin", Some("B/Twin.md")),
            ("b/twin", Some("B/Twin.md")),
            ("Wrong/Twin", None),
            ("/B/Twin", Some("B/Twin.md")),
            ("/Twin", Some("Twin.md")),
            ("tWiN", Some("A/Twin.md")),
            ("Twin.md", Some("A/Twin.md")),
            ("Twin#Section|Display", Some("A/Twin.md")),
            ("photo.png", Some("assets/photo.png")),
            ("Absent", None),
            ("LOWER", Some("Sub/lower.md")),
            ("/b/twin", Some("B/Twin.md")),
            ("/a/twin", Some("A/Twin.md")),
            ("/Absent", None),
            ("../Twin", None),
            ("A/../Twin", None),
            (".hidden", None),
        ];
        for (link, expected) in cases {
            let got = vault.resolve_link(link, "A/Source.md").unwrap();
            assert_eq!(got.as_deref(), expected, "[[{link}]]");
        }
    }

    #[test]
    fn link_at_uses_utf16_caret_and_half_open_range() {
        let text = "😀 [[A]] x";
        // The emoji is two UTF-16 units; `[[` starts at 3 and `]]` ends at 8.
        assert_eq!(link_at(text, 2), None);
        assert_eq!(link_at(text, 3), Some("A"));
        assert_eq!(link_at(text, 7), Some("A"));
        assert_eq!(link_at(text, 8), None);
        assert_eq!(link_at("[[A]]", 1000), None);
    }

    #[test]
    fn following_selects_notes_and_blocks_dirty_navigation() {
        let (_dir, _vault) = vault();
        let mut session = Session::new();
        session.open_vault(_dir.path()).unwrap();
        session.select("A/Source.md").unwrap();
        let text = session.content().to_owned();
        assert_eq!(session.follow_link(&text, 0).unwrap(), Followed::Note);
        assert_eq!(session.path(), Some("A/Twin.md"));
        session.select("A/Source.md").unwrap();
        let absent = text.find("[[Absent]]").unwrap() + 2;
        assert_eq!(
            session.follow_link(&text, absent).unwrap(),
            Followed::Unresolved
        );
        assert_eq!(
            session.follow_link(&text, text.len()).unwrap(),
            Followed::NoLink
        );
        session.set_content(format!("{text} edited"));
        let edited = session.content().to_owned();
        let photo = edited.find("[[photo.png]]").unwrap() + 2;
        assert!(matches!(session.follow_link(&edited, 0), Err(Error::Dirty)));
        assert!(matches!(
            session.follow_link(&edited, photo),
            Err(Error::Dirty)
        ));
        assert_eq!(session.path(), Some("A/Source.md"));
    }

    #[test]
    fn following_self_and_attachments_keeps_the_note() {
        let (dir, _vault) = vault();
        let mut session = Session::new();
        session.open_vault(dir.path()).unwrap();
        session.select("A/Twin.md").unwrap();
        let edited = "edited [[Twin#Section|Display]] [[photo.png]]";
        session.set_content(edited.to_owned());
        assert_eq!(session.follow_link(edited, 9).unwrap(), Followed::Note);
        assert_eq!(session.path(), Some("A/Twin.md"));
        assert_eq!(session.content(), edited);
        session.discard_edits();
        let text = "[[photo.png]] [[Twin#Section|Display]]";
        let canonical = fs::canonicalize(dir.path().join("assets/photo.png")).unwrap();
        assert_eq!(
            session.follow_link(text, 2).unwrap(),
            Followed::Attachment("assets/photo.png".into(), canonical)
        );
        assert_eq!(session.path(), Some("A/Twin.md"));
        assert_eq!(
            fs::read(dir.path().join("A/Twin.md")).unwrap(),
            b"A target\n\n# Section\n\nHeading navigation is not implemented.\n"
        );
    }

    #[test]
    fn missing_attachment_is_unresolved_and_not_opened() {
        let (dir, _vault) = vault();
        let mut session = Session::new();
        session.open_vault(dir.path()).unwrap();
        session.select("A/Source.md").unwrap();
        let text = session.content().to_owned();
        let photo = text.find("[[photo.png]]").unwrap() + 2;
        fs::remove_file(dir.path().join("assets/photo.png")).unwrap();
        assert_eq!(
            session.follow_link(&text, photo).unwrap(),
            Followed::Unresolved
        );
    }

    #[test]
    fn attachment_is_revalidated_inside_the_vault() {
        let (dir, vault) = vault();
        let canonical = fs::canonicalize(dir.path().join("assets/photo.png")).unwrap();
        assert_eq!(
            vault.attachment_path("assets/photo.png").unwrap(),
            canonical
        );
        for bad in [
            "",
            "Twin.md",
            "../x",
            "assets/../Twin.md",
            "/etc/passwd",
            "assets//photo.png",
            ".git/config",
            "assets\\photo.png",
            "assets/missing.png",
            "assets",
        ] {
            assert!(vault.attachment_path(bad).is_err(), "{bad:?}");
        }
        let outside = tempfile::tempdir().unwrap();
        fs::write(outside.path().join("secret.txt"), b"x").unwrap();
        #[cfg(unix)]
        {
            use std::os::unix::fs::symlink;
            symlink(
                outside.path().join("secret.txt"),
                dir.path().join("link.txt"),
            )
            .unwrap();
            symlink(outside.path(), dir.path().join("linked")).unwrap();
            assert!(vault.attachment_path("link.txt").is_err());
            assert!(vault.attachment_path("linked/secret.txt").is_err());
            // Symlinks are invisible to discovery, so links cannot resolve to them.
            assert_eq!(vault.resolve_link("link.txt", "Twin.md").unwrap(), None);
            assert_eq!(
                vault.resolve_link("linked/secret.txt", "Twin.md").unwrap(),
                None
            );
        }
    }
}
