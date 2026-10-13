use inkstow_core::watch::{self, DiskWatch};
use inkstow_core::{DiskChange, Error, Session};
use serde::Serialize;
use std::ops::{Deref, DerefMut};
use std::sync::Mutex;
use tauri::{Emitter, Manager, State};

#[derive(Serialize)]
struct Snapshot {
    root: Option<String>,
    notes: Vec<String>,
    folders: Vec<String>,
    path: Option<String>,
    content: String,
    dirty: bool,
    /// False when the UI must poll because native watches are incomplete.
    watching: bool,
}
/// Every command rescans, so reconcile watches with the new view of the disk.
fn snapshot(app: &mut App) -> Result<Snapshot, String> {
    let listed = match app.session.vault() {
        Some(vault) => vault.list(),
        None => Ok((vec![], vec![])),
    };
    // Reconcile even if the vault folder vanished, so its ancestor reports its return.
    let folders = listed.as_ref().map_or(&[][..], |(_, folders)| folders);
    let watching = app.watch.reconcile(&watch::targets(&app.session, folders));
    let (notes, folders) = listed.map_err(|e| e.to_string())?;
    let session = &app.session;
    Ok(Snapshot {
        root: session.vault().map(|v| v.root().display().to_string()),
        notes,
        folders,
        path: session.path().map(str::to_owned),
        content: session.content().to_owned(),
        dirty: session.dirty(),
        watching,
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
struct App {
    session: Session,
    watch: DiskWatch,
}
impl Deref for App {
    type Target = Session;
    fn deref(&self) -> &Session {
        &self.session
    }
}
impl DerefMut for App {
    fn deref_mut(&mut self) -> &mut Session {
        &mut self.session
    }
}
type AppState = Mutex<App>;
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
        snapshot: snapshot(&mut session)?,
        change,
    })
}
#[tauri::command]
fn reload_from_disk(state: State<'_, AppState>) -> Result<Snapshot, String> {
    let mut session = state.lock().map_err(|e| e.to_string())?;
    session.reload_from_disk().map_err(|e| e.to_string())?;
    snapshot(&mut session)
}
#[tauri::command]
fn keep_mine(state: State<'_, AppState>) -> Result<Snapshot, String> {
    let mut session = state.lock().map_err(|e| e.to_string())?;
    session.overwrite_disk().map_err(|e| e.to_string())?;
    snapshot(&mut session)
}
#[tauri::command]
fn merge_outside(state: State<'_, AppState>) -> Result<Snapshot, String> {
    let mut session = state.lock().map_err(|e| e.to_string())?;
    session.merge_outside_changes().map_err(|e| e.to_string())?;
    snapshot(&mut session)
}
#[tauri::command]
fn save_as_new(state: State<'_, AppState>) -> Result<Snapshot, String> {
    let mut session = state.lock().map_err(|e| e.to_string())?;
    session.save_as_new_note().map_err(|e| e.to_string())?;
    snapshot(&mut session)
}
#[tauri::command]
fn discard_edits(state: State<'_, AppState>) -> Result<Snapshot, String> {
    let mut session = state.lock().map_err(|e| e.to_string())?;
    session.discard_edits();
    snapshot(&mut session)
}
#[tauri::command]
fn view(state: State<'_, AppState>) -> Result<Snapshot, String> {
    let mut session = state.lock().map_err(|e| e.to_string())?;
    snapshot(&mut session)
}
#[tauri::command]
fn open_vault(folder: String, state: State<'_, AppState>) -> Result<Snapshot, String> {
    let mut session = state.lock().map_err(|e| e.to_string())?;
    session.open_vault(folder).map_err(|e| e.to_string())?;
    snapshot(&mut session)
}
#[tauri::command]
fn select_note(path: String, state: State<'_, AppState>) -> Result<Snapshot, String> {
    let mut session = state.lock().map_err(|e| e.to_string())?;
    session.select(&path).map_err(|e| e.to_string())?;
    snapshot(&mut session)
}
#[tauri::command]
fn trash_note(state: State<'_, AppState>) -> Result<Snapshot, String> {
    let mut session = state.lock().map_err(|e| e.to_string())?;
    session.trash_note().map_err(|e| e.to_string())?;
    snapshot(&mut session)
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
        snapshot: snapshot(&mut session)?,
        warning,
    })
}
#[tauri::command]
fn edit(content: String, state: State<'_, AppState>) -> Result<Snapshot, String> {
    let mut session = state.lock().map_err(|e| e.to_string())?;
    session.set_content(content);
    snapshot(&mut session)
}
#[tauri::command]
fn save(state: State<'_, AppState>) -> Result<Snapshot, String> {
    let mut session = state.lock().map_err(|e| e.to_string())?;
    session.save().map_err(|e| e.to_string())?;
    snapshot(&mut session)
}
#[tauri::command]
fn create_note(path: String, state: State<'_, AppState>) -> Result<Snapshot, String> {
    let mut session = state.lock().map_err(|e| e.to_string())?;
    session.create(&path).map_err(|e| e.to_string())?;
    snapshot(&mut session)
}
fn main() {
    tauri::Builder::default()
        .setup(|app| {
            let handle = app.handle().clone();
            let watch = watch::native(move || {
                let _ = handle.emit("disk-changed", ());
            });
            app.manage(Mutex::new(App {
                session: Session::new(),
                watch,
            }));
            Ok(())
        })
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
            discard_edits
        ])
        .run(tauri::generate_context!())
        .expect("failed to run Inkstow Rust preview");
}
