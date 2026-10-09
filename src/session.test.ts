import { describe, expect, it, vi } from "vitest";
import type { VaultApi } from "./api";
import { Session } from "./session";

const makeApi = (): VaultApi => ({
  openVault: vi.fn(async () => ["note.md", "other.md"]),
  listNotes: vi.fn(async () => []),
  readNote: vi.fn(async (path) => path),
  saveNote: vi.fn(async () => {}),
});

describe("session", () => {
  it("opens, edits, saves, and refuses to discard unsaved edits", async () => {
    const api = makeApi();
    const session = new Session(api);
    await session.openVault("/vault");
    await session.selectNote("note.md");
    session.content = "edited [[other]]";
    await expect(session.selectNote("other.md")).rejects.toThrow("Save");
    expect(session.path).toBe("note.md");
    await session.save();
    expect(api.saveNote).toHaveBeenCalledWith("note.md", "edited [[other]]");
    expect(session.dirty).toBe(false);
    await session.selectNote("other.md");
    expect(session.content).toBe("other.md");
  });

  it("keeps unsaved changes when a save finishes after further typing", async () => {
    let complete!: () => void;
    const api = makeApi();
    api.saveNote = vi.fn(() => new Promise<void>((resolve) => { complete = resolve; }));
    const session = new Session(api);
    await session.openVault("/vault");
    await session.selectNote("note.md");
    session.content = "first edit";
    const pending = session.save();
    session.content = "second edit";
    complete();
    await pending;
    expect(session.dirty).toBe(true);
    expect(session.savedContent).toBe("first edit");
  });

  it("retains the prior vault when opening another vault fails", async () => {
    const api = makeApi();
    const session = new Session(api);
    await session.openVault("/vault");
    api.openVault = vi.fn(async () => { throw new Error("not a folder"); });
    await expect(session.openVault("/missing")).rejects.toThrow("not a folder");
    expect(session.notes).toEqual(["note.md", "other.md"]);
  });
});
