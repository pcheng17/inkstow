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
    expect(api.saveNote).toHaveBeenCalledWith("note.md", "note.md", "edited [[other]]");
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
    expect(api.saveNote).toHaveBeenCalledWith("note.md", "note.md", "first edit");
  });

  it("keeps local edits when an external change rejects a second save", async () => {
    let disk = "original";
    const api = makeApi();
    api.readNote = vi.fn(async () => disk);
    api.saveNote = vi.fn(async (_path, expected, content) => {
      if (disk !== expected) throw new Error("Save conflict: note changed on disk");
      disk = content;
    });
    const session = new Session(api);
    await session.openVault("/vault");
    await session.selectNote("note.md");
    session.content = "first edit";
    await session.save();
    expect(disk).toBe("first edit");
    session.content = "local unsaved edit";
    disk = "external edit";
    await expect(session.save()).rejects.toThrow("Save conflict");
    expect(api.saveNote).toHaveBeenLastCalledWith("note.md", "first edit", "local unsaved edit");
    expect(disk).toBe("external edit");
    expect(session.content).toBe("local unsaved edit");
    expect(session.savedContent).toBe("first edit");
    expect(session.dirty).toBe(true);
    await expect(session.selectNote("other.md")).rejects.toThrow("Save");
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
