//! Follow-link command glue. Status text matches the Qt app; attachments reach the
//! system opener only after the core revalidates them inside the vault.
use inkstow_core::{Error, Followed, Session};
use std::io;
use std::path::Path;
use std::process::Command;

/// Follow the link at `caret` in the editor's own `text`, whose line endings may
/// differ from the stored note (a textarea reports CRLF notes with LF), and
/// describe the outcome.
pub fn follow(
    session: &mut Session,
    text: &str,
    caret: usize,
    open: impl FnOnce(&Path) -> io::Result<()>,
) -> Result<String, String> {
    match session
        .follow_link(text, caret)
        .map_err(|e| e.to_string())?
    {
        Followed::NoLink => Ok("Place the cursor inside a [[link]] to follow it.".into()),
        Followed::Unresolved => Ok("Unresolved link".into()),
        Followed::Note => Ok(format!("Opened {}", session.path().unwrap_or_default())),
        Followed::Attachment(target, path) => match open(&path) {
            Ok(()) => Ok(format!("Opened attachment: {target}")),
            Err(_) => Err(Error::Attachment(target).to_string()),
        },
    }
}

/// Open a file with the system default app, without a shell.
pub fn open_with_system(path: &Path) -> io::Result<()> {
    #[cfg(target_os = "macos")]
    let mut command = Command::new("open");
    #[cfg(windows)]
    let mut command = Command::new("explorer");
    #[cfg(not(any(target_os = "macos", windows)))]
    let mut command = Command::new("xdg-open");
    #[cfg(windows)]
    let path = &windows_path(path);
    let mut child = command.arg(path).spawn()?;
    // Reap the launcher so it does not linger as a zombie.
    std::thread::spawn(move || child.wait());
    Ok(())
}

// Explorer does not accept the verbatim `\\?\` paths that canonicalize returns.
#[cfg(windows)]
fn windows_path(path: &Path) -> std::path::PathBuf {
    let text = path.to_string_lossy();
    match text.strip_prefix(r"\\?\UNC\") {
        Some(rest) => format!(r"\\{rest}").into(),
        None => text.strip_prefix(r"\\?\").unwrap_or(&text).into(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    fn session(text: &str) -> (tempfile::TempDir, Session) {
        let dir = tempfile::tempdir().unwrap();
        fs::create_dir_all(dir.path().join("assets")).unwrap();
        fs::create_dir_all(dir.path().join(".hidden")).unwrap();
        fs::write(dir.path().join("Source.md"), text).unwrap();
        fs::write(dir.path().join("Target.md"), "target").unwrap();
        fs::write(dir.path().join("assets/photo.png"), b"png").unwrap();
        fs::write(dir.path().join(".hidden/secret.png"), b"x").unwrap();
        let mut session = Session::new();
        session.open_vault(dir.path()).unwrap();
        session.select("Source.md").unwrap();
        (dir, session)
    }
    // Follow with the editor showing the session's text unchanged.
    fn follow_saved(
        session: &mut Session,
        caret: usize,
        open: impl FnOnce(&Path) -> io::Result<()>,
    ) -> Result<String, String> {
        let text = session.content().to_owned();
        follow(session, &text, caret, open)
    }
    fn never(_: &Path) -> io::Result<()> {
        panic!("opener must not run")
    }

    #[test]
    fn notes_unresolved_and_caret_outside_report_like_qt() {
        let (_dir, mut session) = session("[[Target#Part|Alias]] [[Missing]] x");
        assert_eq!(
            follow_saved(&mut session, 34, never).unwrap(),
            "Place the cursor inside a [[link]] to follow it."
        );
        assert_eq!(
            follow_saved(&mut session, 25, never).unwrap(),
            "Unresolved link"
        );
        assert_eq!(
            follow_saved(&mut session, 2, never).unwrap(),
            "Opened Target.md"
        );
        assert_eq!(session.content(), "target");
    }

    #[test]
    fn caret_counts_in_the_editor_text_for_crlf_notes() {
        let (_dir, mut session) = session("a\r\n\r\n[[Missing]][[Target]]");
        // The textarea shows LF line endings; caret 15 is on `[[Target]]` there.
        let shown = "a\n\n[[Missing]][[Target]]";
        assert_eq!(
            follow(&mut session, shown, 15, never).unwrap(),
            "Opened Target.md"
        );
    }

    #[test]
    fn attachment_opens_canonical_path_inside_the_vault() {
        let (dir, mut session) = session("[[photo.png]]");
        let mut opened = None;
        let message = follow_saved(&mut session, 2, |path| {
            opened = Some(path.to_owned());
            Ok(())
        })
        .unwrap();
        assert_eq!(message, "Opened attachment: assets/photo.png");
        let expected = fs::canonicalize(dir.path().join("assets/photo.png")).unwrap();
        assert_eq!(opened, Some(expected));
        assert_eq!(session.path(), Some("Source.md"));
        let failed = follow_saved(&mut session, 2, |_| Err(io::ErrorKind::NotFound.into()));
        assert_eq!(
            failed.unwrap_err(),
            "Could not open attachment: assets/photo.png"
        );
    }

    #[test]
    fn dirty_hidden_traversal_and_symlinks_never_reach_the_opener() {
        let text = "[[photo.png]] [[Target]] [[.hidden/secret.png]] [[../x.png]] [[out.png]]";
        let (dir, mut session) = session(text);
        #[cfg(unix)]
        {
            let outside = tempfile::tempdir().unwrap();
            fs::write(outside.path().join("out.png"), b"x").unwrap();
            std::os::unix::fs::symlink(outside.path().join("out.png"), dir.path().join("out.png"))
                .unwrap();
        }
        for caret in [16, 27, 50, 63] {
            assert_eq!(
                follow_saved(&mut session, caret, never).unwrap(),
                if caret == 16 {
                    "Opened Target.md"
                } else {
                    "Unresolved link"
                },
                "caret {caret}"
            );
            if caret == 16 {
                session.select("Source.md").unwrap();
            }
        }
        session.set_content(format!("{text} edited"));
        for caret in [2, 16] {
            assert_eq!(
                follow_saved(&mut session, caret, never).unwrap_err(),
                "Save the current note before switching"
            );
        }
        assert_eq!(session.path(), Some("Source.md"));
        assert_eq!(
            fs::read_to_string(dir.path().join("Source.md")).unwrap(),
            text
        );
    }
}
