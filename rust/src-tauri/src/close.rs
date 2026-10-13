//! Thin Tauri glue for quit protection; the decisions live in `inkstow_core::CloseGuard`.
use inkstow_core::{CloseAction, CloseGuard, CloseOutcome, Session};
use std::sync::Mutex;
use tauri::{AppHandle, Emitter, RunEvent, State};

pub type GuardState = Mutex<CloseGuard>;

/// Cancels every window close and asks the page to start the guarded flow.
/// The page flushes its queued edits first, so Rust never judges a stale dirty flag.
/// Exit requests pass through: ours come from `AppHandle::exit`, and on desktop the only
/// other source is the last window being destroyed, when no page is left to prompt.
pub fn on_run_event(app: &AppHandle, event: &RunEvent) {
    if let RunEvent::WindowEvent {
        event: tauri::WindowEvent::CloseRequested { api, .. },
        ..
    } = event
    {
        api.prevent_close();
        let _ = app.emit("close-requested", ());
    }
}

#[tauri::command]
pub fn close_app(
    action: String,
    editor: Option<String>,
    app: AppHandle,
    session: State<'_, Mutex<Session>>,
    guard: State<'_, GuardState>,
) -> Result<&'static str, String> {
    let action = match action.as_str() {
        "request" => CloseAction::Request,
        "save" => CloseAction::SaveAndQuit,
        "discard" => CloseAction::DiscardAndQuit,
        "cancel" => CloseAction::Cancel,
        _ => return Err("unknown close action".into()),
    };
    let mut session = session.lock().map_err(|e| e.to_string())?;
    let mut guard = guard.lock().map_err(|e| e.to_string())?;
    let outcome = guard
        .resolve(&mut session, action, editor)
        .map_err(|e| e.to_string())?;
    Ok(match outcome {
        CloseOutcome::Exit => {
            app.exit(0);
            "exit"
        }
        CloseOutcome::Prompt => "prompt",
        CloseOutcome::AlreadyPrompting => "already",
        CloseOutcome::Stay => "stay",
    })
}
