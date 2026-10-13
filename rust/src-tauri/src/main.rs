mod close;
use inkstow_core::{DiskChange, Error, Session};
use serde::Serialize;
use std::sync::Mutex;
use tauri::State;

#[derive(Serialize)]
struct Snapshot {
    root: Option<String>,
    notes: Vec<String>,
    folders: Vec<String>,
    path: Option<String>,
    content: String,
    dirty: bool,
}
fn snapshot(session: &Session) -> Result<Snapshot, String> {
    let (notes, folders) = match session.vault() {
        Some(vault) => vault.list().map_err(|e| e.to_string())?,
        None => (vec![], vec![]),
    };
    Ok(Snapshot {
        root: session.vault().map(|v| v.root().display().to_string()),
        notes,
        folders,
        path: session.path().map(str::to_owned),
        content: session.content().to_owned(),
        dirty: session.dirty(),
    })
}
#[derive(Serialize)]
struct SyncResult {
    snapshot: Snapshot,
    change: &'static str,
}
#[derive(Serialize)]
struct MoveResult {
    snapshot: Snapshot,
    warning: Option<String>,
}
type AppState = Mutex<Session>;
#[tauri::command]
fn sync_disk(state: State<'_, AppState>) -> Result<SyncResult, String> {
    let mut session = state.lock().map_err(|e| e.to_string())?;
    let change = match session.sync_with_disk() {
        DiskChange::None => "none",
        DiskChange::Reloaded => "reloaded",
        DiskChange::Conflict => "conflict",
        DiskChange::Missing => "missing",
    };
    Ok(SyncResult {
        snapshot: snapshot(&session)?,
        change,
    })
}
#[tauri::command]
fn reload_from_disk(state: State<'_, AppState>) -> Result<Snapshot, String> {
    let mut session = state.lock().map_err(|e| e.to_string())?;
    session.reload_from_disk().map_err(|e| e.to_string())?;
    snapshot(&session)
}
#[tauri::command]
fn keep_mine(state: State<'_, AppState>) -> Result<Snapshot, String> {
    let mut session = state.lock().map_err(|e| e.to_string())?;
    session.overwrite_disk().map_err(|e| e.to_string())?;
    snapshot(&session)
}
#[tauri::command]
fn merge_outside(state: State<'_, AppState>) -> Result<Snapshot, String> {
    let mut session = state.lock().map_err(|e| e.to_string())?;
    session.merge_outside_changes().map_err(|e| e.to_string())?;
    snapshot(&session)
}
#[tauri::command]
fn save_as_new(state: State<'_, AppState>) -> Result<Snapshot, String> {
    let mut session = state.lock().map_err(|e| e.to_string())?;
    session.save_as_new_note().map_err(|e| e.to_string())?;
    snapshot(&session)
}
#[tauri::command]
fn discard_edits(state: State<'_, AppState>) -> Result<Snapshot, String> {
    let mut session = state.lock().map_err(|e| e.to_string())?;
    session.discard_edits();
    snapshot(&session)
}
#[tauri::command]
fn view(state: State<'_, AppState>) -> Result<Snapshot, String> {
    let session = state.lock().map_err(|e| e.to_string())?;
    snapshot(&session)
}
#[tauri::command]
fn open_vault(folder: String, state: State<'_, AppState>) -> Result<Snapshot, String> {
    let mut session = state.lock().map_err(|e| e.to_string())?;
    session.open_vault(folder).map_err(|e| e.to_string())?;
    snapshot(&session)
}
#[tauri::command]
fn select_note(path: String, state: State<'_, AppState>) -> Result<Snapshot, String> {
    let mut session = state.lock().map_err(|e| e.to_string())?;
    session.select(&path).map_err(|e| e.to_string())?;
    snapshot(&session)
}
#[tauri::command]
fn trash_note(state: State<'_, AppState>) -> Result<Snapshot, String> {
    let mut session = state.lock().map_err(|e| e.to_string())?;
    session.trash_note().map_err(|e| e.to_string())?;
    snapshot(&session)
}
#[tauri::command]
fn move_note(
    path: String,
    update_links: bool,
    state: State<'_, AppState>,
) -> Result<MoveResult, String> {
    let mut session = state.lock().map_err(|e| e.to_string())?;
    let warning = match session.move_note(&path, update_links) {
        Ok(()) => None,
        Err(error @ Error::PartialMove(_)) => Some(error.to_string()),
        Err(error) => return Err(error.to_string()),
    };
    Ok(MoveResult {
        snapshot: snapshot(&session)?,
        warning,
    })
}
#[tauri::command]
fn edit(content: String, state: State<'_, AppState>) -> Result<Snapshot, String> {
    let mut session = state.lock().map_err(|e| e.to_string())?;
    session.set_content(content);
    snapshot(&session)
}
#[tauri::command]
fn save(state: State<'_, AppState>) -> Result<Snapshot, String> {
    let mut session = state.lock().map_err(|e| e.to_string())?;
    session.save().map_err(|e| e.to_string())?;
    snapshot(&session)
}
#[tauri::command]
fn create_note(path: String, state: State<'_, AppState>) -> Result<Snapshot, String> {
    let mut session = state.lock().map_err(|e| e.to_string())?;
    session.create(&path).map_err(|e| e.to_string())?;
    snapshot(&session)
}
fn main() {
    tauri::Builder::default()
        .manage(Mutex::new(Session::new()))
        .manage(close::GuardState::default())
        .invoke_handler(tauri::generate_handler![
            view,
            open_vault,
            select_note,
            edit,
            save,
            create_note,
            trash_note,
            move_note,
            sync_disk,
            reload_from_disk,
            keep_mine,
            merge_outside,
            save_as_new,
            discard_edits,
            close::close_app
        ])
        .build(tauri::generate_context!())
        .expect("failed to build Inkstow Rust preview")
        .run(|app, event| close::on_run_event(app, &event));
}
