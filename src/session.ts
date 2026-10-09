import type { VaultApi } from "./api";

export class Session {
  notes: string[] = [];
  path: string | null = null;
  content = "";
  savedContent = "";

  constructor(private readonly api: VaultApi) {}

  get dirty(): boolean {
    return this.content !== this.savedContent;
  }

  async openVault(folder: string): Promise<void> {
    if (this.dirty) throw new Error("Save the current note before opening another vault.");
    const notes = await this.api.openVault(folder);
    this.notes = notes;
    this.path = null;
    this.content = "";
    this.savedContent = "";
  }

  async selectNote(path: string): Promise<void> {
    if (this.dirty) throw new Error("Save the current note before switching.");
    if (!this.notes.includes(path)) throw new Error("Note is not in the current vault.");
    const content = await this.api.readNote(path);
    this.path = path;
    this.content = content;
    this.savedContent = content;
  }

  async save(): Promise<void> {
    if (!this.path) throw new Error("Select a note first.");
    const path = this.path;
    const content = this.content;
    await this.api.saveNote(path, content);
    // Do not mark newer, unsaved edits as saved if a save finishes late.
    if (this.path === path) this.savedContent = content;
  }
}
