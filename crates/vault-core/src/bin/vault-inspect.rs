use std::env;
use std::process::ExitCode;
use vault_core::Vault;

fn main() -> ExitCode {
    let Some(folder) = env::args_os().nth(1) else {
        eprintln!("Usage: vault-inspect <vault-folder>");
        return ExitCode::FAILURE;
    };
    let result = Vault::open(folder).and_then(|vault| vault.list_notes());
    match result {
        Ok(notes) => {
            for note in notes {
                println!("{note}");
            }
            ExitCode::SUCCESS
        }
        Err(error) => {
            eprintln!("Vault error: {error}");
            ExitCode::FAILURE
        }
    }
}
