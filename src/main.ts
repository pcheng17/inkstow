import { EditorView, basicSetup } from "codemirror";
import { markdown } from "@codemirror/lang-markdown";
import { vaultApi } from "./api";
import { Session } from "./session";
import "./style.css";

const session = new Session(vaultApi);
const app = document.querySelector<HTMLDivElement>("#app")!;
app.innerHTML = `
  <header><h1>Inkstow</h1><form id="open-form"><label for="folder">Vault folder</label>
    <input id="folder" type="text" placeholder="/path/to/vault" required />
    <button type="submit">Open</button></form></header>
  <main><aside><h2>Notes</h2><ul id="notes"></ul></aside>
    <section><div class="toolbar"><strong id="filename">Select a note</strong>
      <button id="save" type="button" disabled>Save</button></div>
      <div id="editor"></div></section></main>
  <footer id="status" role="status">Open a vault folder to begin.</footer>`;

const noteList = document.querySelector<HTMLUListElement>("#notes")!;
const filename = document.querySelector<HTMLElement>("#filename")!;
const saveButton = document.querySelector<HTMLButtonElement>("#save")!;
const status = document.querySelector<HTMLElement>("#status")!;
const openForm = document.querySelector<HTMLFormElement>("#open-form")!;
const folderInput = document.querySelector<HTMLInputElement>("#folder")!;
let busy = false;

const editor = new EditorView({
  parent: document.querySelector<HTMLElement>("#editor")!,
  extensions: [basicSetup, markdown(), EditorView.updateListener.of((update) => {
    if (update.docChanged) {
      session.content = update.state.doc.toString();
      refresh();
    }
  })],
});

function refresh(): void {
  filename.textContent = session.path ? `${session.path}${session.dirty ? " *" : ""}` : "Select a note";
  saveButton.disabled = busy || !session.dirty;
  for (const button of noteList.querySelectorAll<HTMLButtonElement>("button")) {
    button.classList.toggle("selected", button.dataset.path === session.path);
    button.disabled = busy;
  }
}

function replaceEditor(content: string): void {
  editor.dispatch({ changes: { from: 0, to: editor.state.doc.length, insert: content } });
}

function showNotes(): void {
  noteList.replaceChildren();
  for (const path of session.notes) {
    const item = document.createElement("li");
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = path;
    button.dataset.path = path;
    button.addEventListener("click", () => run(async () => {
      await session.selectNote(path);
      replaceEditor(session.content);
      editor.focus();
      status.textContent = `Opened ${path}`;
    }));
    item.append(button);
    noteList.append(item);
  }
  refresh();
}

async function run(operation: () => Promise<void>): Promise<void> {
  if (busy) return;
  busy = true;
  refresh();
  try {
    await operation();
  } catch (error) {
    status.textContent = `Error: ${String(error)}`;
  } finally {
    busy = false;
    refresh();
  }
}

openForm.addEventListener("submit", (event) => {
  event.preventDefault();
  void run(async () => {
    await session.openVault(folderInput.value.trim());
    replaceEditor("");
    showNotes();
    status.textContent = `Found ${session.notes.length} notes`;
  });
});

saveButton.addEventListener("click", () => void run(async () => {
  await session.save();
  status.textContent = session.dirty ? "Saved; more changes pending" : "Saved";
}));

window.addEventListener("beforeunload", (event) => {
  if (session.dirty) event.preventDefault();
});
