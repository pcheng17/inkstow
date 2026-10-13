//! Guarded note move with conservative wikilink rewrites. No filesystem transaction:
//! a failure after rename may leave the moved note and some updated backlinks.
use crate::links::{basename, eq, link_regex, resolve, visible_files};
use crate::{markdown, Error, Result, Vault};
use std::fs;
use std::path::Path;

struct Rewrite {
    path: String,
    before: Vec<u8>,
    after: Vec<u8>,
}

fn safe_link_path(path: &str) -> bool {
    !path
        .chars()
        .any(|c| matches!(c, '#' | '|' | '[' | ']' | '\n' | '\r'))
}
fn shadowed(root: &Path, link_path: &str) -> Result<bool> {
    let path = root.join(link_path);
    let parent = path.parent().expect("relative link has parent");
    let name = path
        .file_name()
        .expect("nonempty link path")
        .to_string_lossy();
    for entry in fs::read_dir(parent)? {
        let entry = entry?;
        if eq(&entry.file_name().to_string_lossy(), &name) {
            let ty = entry.file_type()?;
            if ty.is_file() || ty.is_symlink() {
                return Ok(true);
            }
        }
    }
    Ok(false)
}

#[cfg(any(target_os = "linux", target_os = "macos"))]
fn rename_no_replace(source: &Path, destination: &Path) -> std::io::Result<()> {
    rustix::fs::renameat_with(
        rustix::fs::CWD,
        source,
        rustix::fs::CWD,
        destination,
        rustix::fs::RenameFlags::NOREPLACE,
    )
    .map_err(std::io::Error::from)
}
#[cfg(not(any(target_os = "linux", target_os = "macos")))]
fn rename_no_replace(source: &Path, destination: &Path) -> std::io::Result<()> {
    // Windows rename refuses an existing destination; other platforms need
    // an atomic no-replace primitive before they are supported for releases.
    fs::rename(source, destination)
}

impl Vault {
    pub fn move_note(
        &self,
        from: &str,
        to: &str,
        expected: &[u8],
        update_links: bool,
    ) -> Result<()> {
        let source = self.checked_path(from)?;
        let parts = markdown(to)?;
        if from == to || parts.iter().any(|p| p.starts_with('.')) {
            return Err(Error::InvalidPath);
        }
        let mut parent = self.root().to_path_buf();
        for part in &parts[..parts.len() - 1] {
            parent.push(part);
            let metadata = fs::symlink_metadata(&parent).map_err(|_| Error::InvalidPath)?;
            if metadata.file_type().is_symlink() || !metadata.is_dir() {
                return Err(Error::InvalidPath);
            }
        }
        let destination = parent.join(parts.last().expect("nonempty"));
        if fs::symlink_metadata(&destination).is_ok() {
            return Err(Error::DestinationExists);
        }
        if self.read(from)? != expected {
            return Err(Error::Conflict);
        }
        let mut rewrites = Vec::new();
        let mut files_snapshot = None;
        if update_links {
            if !safe_link_path(to) || to[..to.len() - 3].to_ascii_lowercase().ends_with(".md") {
                return Err(Error::UnsafeLink);
            }
            let notes = self.list()?.0;
            let links = link_regex();
            let old_name = &basename(from)[..basename(from).len() - 3];
            let new_name = &basename(to)[..basename(to).len() - 3];
            for note in notes {
                let bytes = self.read(&note)?;
                let bom = bytes.starts_with(&[0xef, 0xbb, 0xbf]);
                let text = std::str::from_utf8(if bom { &bytes[3..] } else { &bytes })
                    .map_err(|_| Error::InvalidUtf8)?;
                if text.matches("[[").count() != text.matches("]]").count() {
                    return Err(Error::UnsupportedLink(note));
                }
                let mut output = String::new();
                let mut offset = 0;
                let mut count = 0;
                for capture in links.captures_iter(text) {
                    count += 1;
                    let full = capture.get(0).expect("full match");
                    let link = capture.get(1).expect("link capture").as_str();
                    let bare = basename(link)
                        .split('#')
                        .next()
                        .unwrap_or("")
                        .split('|')
                        .next()
                        .unwrap_or("");
                    let relevant = note == from
                        || [
                            old_name,
                            new_name,
                            &format!("{old_name}.md"),
                            &format!("{new_name}.md"),
                        ]
                        .iter()
                        .any(|name| eq(bare, name));
                    if !relevant {
                        continue;
                    }
                    if link.contains('|')
                        || link.contains('#')
                        || link.starts_with('.')
                        || text[..full.start()].ends_with('!')
                    {
                        return Err(Error::ComplexLink(note));
                    }
                    // Recheck the source even though discovery was shared.
                    self.checked_path(&note)?;
                    if files_snapshot.is_none() {
                        files_snapshot = Some(visible_files(self.root())?);
                    }
                    let target =
                        resolve(link, &note, files_snapshot.as_ref().expect("initialized"))
                            .filter(|p| p.to_ascii_lowercase().ends_with(".md"))
                            .ok_or_else(|| Error::UnsupportedLink(note.clone()))?;
                    output.push_str(&text[offset..full.start()]);
                    let mapped = if target == from { to } else { &target };
                    if !safe_link_path(mapped) {
                        return Err(Error::UnsafeLink);
                    }
                    let link_path = &mapped[..mapped.len() - 3];
                    if link_path.to_ascii_lowercase().ends_with(".md")
                        || shadowed(self.root(), link_path)?
                    {
                        return Err(Error::AmbiguousLink(note));
                    }
                    output.push_str("[[/");
                    output.push_str(link_path);
                    output.push_str("]]");
                    offset = full.end();
                }
                if count != text.matches("[[").count() {
                    return Err(Error::UnsupportedLink(note));
                }
                output.push_str(&text[offset..]);
                let after = [
                    if bom { &[0xef, 0xbb, 0xbf][..] } else { &[] },
                    output.as_bytes(),
                ]
                .concat();
                if after != bytes {
                    rewrites.push(Rewrite {
                        path: note,
                        before: bytes,
                        after,
                    });
                }
            }
        }
        for rewrite in &rewrites {
            if self.read(&rewrite.path)? != rewrite.before {
                return Err(Error::Conflict);
            }
        }
        if self.read(from)? != expected {
            return Err(Error::Conflict);
        }
        if let Some(files) = files_snapshot {
            if visible_files(self.root())? != files {
                return Err(Error::OutsideChanged);
            }
        }
        if fs::symlink_metadata(&destination).is_ok() {
            return Err(Error::DestinationExists);
        }
        rename_no_replace(&source, &destination).map_err(Error::Io)?;
        for rewrite in rewrites {
            let path = if rewrite.path == from {
                to
            } else {
                &rewrite.path
            };
            if let Err(err) = self.save(path, &rewrite.before, &rewrite.after) {
                return Err(Error::PartialMove(err.to_string()));
            }
        }
        Ok(())
    }
}
