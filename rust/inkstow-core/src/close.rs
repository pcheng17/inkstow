//! UI-independent quit protection: decides whether a close may proceed.
use crate::{Result, Session};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CloseAction {
    /// A close or quit was requested by the window, menu, or OS.
    Request,
    SaveAndQuit,
    DiscardAndQuit,
    Cancel,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CloseOutcome {
    Exit,
    /// Show the Save / Discard / Cancel prompt.
    Prompt,
    /// A prompt is already showing; do not open another.
    AlreadyPrompting,
    /// The close was cancelled; keep running.
    Stay,
}

#[derive(Debug, Default)]
pub struct CloseGuard {
    prompting: bool,
}

impl CloseGuard {
    /// `editor` is the UI's current text. It is applied first so a late or stale queued
    /// edit cannot make a dirty note look clean or save older text.
    pub fn resolve(
        &mut self,
        session: &mut Session,
        action: CloseAction,
        editor: Option<String>,
    ) -> Result<CloseOutcome> {
        if let (Some(text), Some(_)) = (editor, session.path()) {
            session.set_content(text);
        }
        match action {
            CloseAction::Request if !session.dirty() => Ok(CloseOutcome::Exit),
            CloseAction::Request if self.prompting => Ok(CloseOutcome::AlreadyPrompting),
            CloseAction::Request => {
                self.prompting = true;
                Ok(CloseOutcome::Prompt)
            }
            CloseAction::SaveAndQuit => {
                // A failed save leaves the edits and window alone; the UI shows the warning.
                self.prompting = false;
                if session.dirty() {
                    session.save()?;
                }
                Ok(CloseOutcome::Exit)
            }
            CloseAction::DiscardAndQuit => Ok(CloseOutcome::Exit),
            CloseAction::Cancel => {
                self.prompting = false;
                Ok(CloseOutcome::Stay)
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::Error;
    use std::fs;

    fn dirty_vault() -> (tempfile::TempDir, Session, CloseGuard) {
        let dir = tempfile::tempdir().unwrap();
        fs::write(dir.path().join("Note.md"), b"original").unwrap();
        let mut session = Session::new();
        session.open_vault(dir.path()).unwrap();
        session.select("Note.md").unwrap();
        session.set_content("edited".into());
        (dir, session, CloseGuard::default())
    }

    #[test]
    fn clean_close_exits_without_prompt() {
        let (dir, mut s, mut g) = dirty_vault();
        s.discard_edits();
        let out = g.resolve(&mut s, CloseAction::Request, Some("original".into()));
        assert_eq!(out.unwrap(), CloseOutcome::Exit);
        assert_eq!(fs::read(dir.path().join("Note.md")).unwrap(), b"original");
    }

    #[test]
    fn no_note_closes_without_prompt() {
        let mut g = CloseGuard::default();
        let out = g.resolve(&mut Session::new(), CloseAction::Request, Some("x".into()));
        assert_eq!(out.unwrap(), CloseOutcome::Exit);
    }

    #[test]
    fn stale_editor_text_is_applied_before_deciding() {
        let (_dir, mut s, mut g) = dirty_vault();
        s.discard_edits();
        let out = g.resolve(&mut s, CloseAction::Request, Some("typed late".into()));
        assert_eq!(out.unwrap(), CloseOutcome::Prompt);
        assert_eq!(s.content(), "typed late");
    }

    #[test]
    fn repeated_requests_prompt_once_and_cancel_keeps_text() {
        let (dir, mut s, mut g) = dirty_vault();
        let req = |g: &mut CloseGuard, s: &mut Session| {
            g.resolve(s, CloseAction::Request, Some("edited".into()))
                .unwrap()
        };
        assert_eq!(req(&mut g, &mut s), CloseOutcome::Prompt);
        assert_eq!(req(&mut g, &mut s), CloseOutcome::AlreadyPrompting);
        let out = g.resolve(&mut s, CloseAction::Cancel, Some("edited".into()));
        assert_eq!(out.unwrap(), CloseOutcome::Stay);
        assert_eq!(s.content(), "edited");
        assert!(s.dirty());
        assert_eq!(fs::read(dir.path().join("Note.md")).unwrap(), b"original");
        assert_eq!(req(&mut g, &mut s), CloseOutcome::Prompt);
    }

    #[test]
    fn save_and_quit_writes_then_exits() {
        let (dir, mut s, mut g) = dirty_vault();
        g.resolve(&mut s, CloseAction::Request, None).unwrap();
        let out = g.resolve(
            &mut s,
            CloseAction::SaveAndQuit,
            Some("edited again".into()),
        );
        assert_eq!(out.unwrap(), CloseOutcome::Exit);
        assert_eq!(
            fs::read(dir.path().join("Note.md")).unwrap(),
            b"edited again"
        );
        assert!(!s.dirty());
    }

    #[test]
    fn save_conflict_keeps_edits_and_disk() {
        let (dir, mut s, mut g) = dirty_vault();
        g.resolve(&mut s, CloseAction::Request, None).unwrap();
        fs::write(dir.path().join("Note.md"), b"outside").unwrap();
        let out = g.resolve(&mut s, CloseAction::SaveAndQuit, Some("edited".into()));
        assert!(matches!(out, Err(Error::Conflict)));
        assert_eq!(s.content(), "edited");
        assert!(s.dirty());
        assert_eq!(fs::read(dir.path().join("Note.md")).unwrap(), b"outside");
        // The prompt is closed, so a later close request prompts again.
        let again = g.resolve(&mut s, CloseAction::Request, None);
        assert_eq!(again.unwrap(), CloseOutcome::Prompt);
    }

    #[test]
    fn save_missing_file_keeps_edits_and_does_not_recreate() {
        let (dir, mut s, mut g) = dirty_vault();
        g.resolve(&mut s, CloseAction::Request, None).unwrap();
        fs::remove_file(dir.path().join("Note.md")).unwrap();
        let out = g.resolve(&mut s, CloseAction::SaveAndQuit, None);
        assert!(out.is_err());
        assert!(s.dirty());
        assert_eq!(s.content(), "edited");
        assert!(!dir.path().join("Note.md").exists());
    }

    #[test]
    fn save_and_quit_after_saving_elsewhere_exits_without_writing() {
        let (dir, mut s, mut g) = dirty_vault();
        g.resolve(&mut s, CloseAction::Request, None).unwrap();
        s.save().unwrap();
        fs::write(dir.path().join("Note.md"), b"outside").unwrap();
        let out = g.resolve(&mut s, CloseAction::SaveAndQuit, None);
        assert_eq!(out.unwrap(), CloseOutcome::Exit);
        assert_eq!(fs::read(dir.path().join("Note.md")).unwrap(), b"outside");
    }

    #[test]
    fn discard_and_quit_leaves_disk_bytes_unchanged() {
        let (dir, mut s, mut g) = dirty_vault();
        g.resolve(&mut s, CloseAction::Request, None).unwrap();
        let out = g.resolve(&mut s, CloseAction::DiscardAndQuit, None);
        assert_eq!(out.unwrap(), CloseOutcome::Exit);
        assert_eq!(fs::read(dir.path().join("Note.md")).unwrap(), b"original");
    }
}
