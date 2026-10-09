import { invoke } from "@tauri-apps/api/core";

export interface VaultApi {
  openVault(path: string): Promise<string[]>;
  listNotes(): Promise<string[]>;
  readNote(path: string): Promise<string>;
  saveNote(path: string, expected: string, content: string): Promise<void>;
}

export const vaultApi: VaultApi = {
  openVault: (path) => invoke<string[]>("open_vault", { path }),
  listNotes: () => invoke<string[]>("list_notes"),
  readNote: (path) => invoke<string>("read_note", { path }),
  saveNote: (path, expected, content) => invoke<void>("save_note", { path, expected, content }),
};
