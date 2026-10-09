use std::sync::Mutex;
use tauri::State;
use vault_core::Vault;

#[derive(Default)]
struct ActiveVault(Mutex<Option<Vault>>);

#[tauri::command]
fn open_vault(path: String, state: State<'_, ActiveVault>) -> Result<Vec<String>, String> {
    let vault = Vault::open(&path).map_err(|error| error.to_string())?;
    let notes = vault.list_notes().map_err(|error| error.to_string())?;
    *state.0.lock().map_err(|error| error.to_string())? = Some(vault);
    Ok(notes)
}

#[tauri::command]
fn list_notes(state: State<'_, ActiveVault>) -> Result<Vec<String>, String> {
    with_vault(state, |vault| vault.list_notes())
}

#[tauri::command]
fn read_note(path: String, state: State<'_, ActiveVault>) -> Result<String, String> {
    with_vault(state, |vault| vault.read_note(&path))
}

#[tauri::command]
fn save_note(
    path: String,
    expected: String,
    content: String,
    state: State<'_, ActiveVault>,
) -> Result<(), String> {
    with_vault(state, |vault| vault.save_note(&path, &expected, &content))
}

fn with_vault<T>(
    state: State<'_, ActiveVault>,
    operation: impl FnOnce(&Vault) -> std::io::Result<T>,
) -> Result<T, String> {
    let guard = state.0.lock().map_err(|error| error.to_string())?;
    let vault = guard.as_ref().ok_or("Open a vault first")?;
    operation(vault).map_err(|error| error.to_string())
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .manage(ActiveVault::default())
        .invoke_handler(tauri::generate_handler![
            open_vault, list_notes, read_note, save_note
        ])
        .run(tauri::generate_context!())
        .expect("failed to run Inkstow");
}
