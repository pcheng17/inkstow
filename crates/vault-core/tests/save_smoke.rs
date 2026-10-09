use std::fs;
use std::path::Path;
use tempfile::tempdir;
use vault_core::Vault;

#[test]
fn saves_a_fixture_copy_and_rejects_an_external_edit_without_losing_either_version() {
    let source = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/sample");
    let scratch = tempdir().unwrap();
    let root = scratch.path();
    fs::create_dir(root.join("Folder")).unwrap();
    fs::create_dir(root.join(".obsidian")).unwrap();
    for relative in ["Welcome.md", "Folder/Second note.md", ".obsidian/app.json"] {
        fs::copy(source.join(relative), root.join(relative)).unwrap();
    }

    let vault = Vault::open(root).unwrap();
    assert_eq!(
        vault.list_notes().unwrap(),
        ["Folder/Second note.md", "Welcome.md"]
    );
    let note = root.join("Welcome.md");
    let config = fs::read(root.join(".obsidian/app.json")).unwrap();
    let original = vault.read_note("Welcome.md").unwrap();
    let saved = format!("{original}\nInkstow edit\n");
    vault.save_note("Welcome.md", &original, &saved).unwrap();
    assert_eq!(fs::read_to_string(&note).unwrap(), saved);

    // The editor's last saved content is the expected baseline for its next save.
    let pending_edit = format!("{saved}\nUnsaved Inkstow edit\n");
    let external_edit = format!("{saved}\nExternal edit\n");
    fs::write(&note, &external_edit).unwrap();
    let error = vault
        .save_note("Welcome.md", &saved, &pending_edit)
        .unwrap_err();
    assert!(error.to_string().contains("Save conflict"));
    assert_eq!(fs::read_to_string(&note).unwrap(), external_edit);
    assert_eq!(fs::read(root.join(".obsidian/app.json")).unwrap(), config);
    assert_eq!(fs::read_dir(root).unwrap().count(), 3); // No leftover temp file.
}
