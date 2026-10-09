use std::path::Path;
use vault_core::Vault;

#[test]
fn fixture_discovery_skips_any_dot_prefixed_path_segment() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/sample");
    let vault = Vault::open(root).unwrap();
    assert_eq!(
        vault.list_notes().unwrap(),
        ["Folder/Second note.md", "Welcome.md"]
    );
}
