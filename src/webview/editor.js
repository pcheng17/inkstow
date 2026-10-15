import { EditorState, Compartment } from '@codemirror/state';
import { EditorView, keymap } from '@codemirror/view';
import { defaultKeymap, history, historyKeymap, undo } from '@codemirror/commands';
import { markdown } from '@codemirror/lang-markdown';
import { livePreview, sourceLinkAt } from './preview.js';

const folder = document.querySelector('#folder');
const form = document.querySelector('#vault-form');
const notes = document.querySelector('#notes');
const title = document.querySelector('#title');
const dirty = document.querySelector('#dirty');
const error = document.querySelector('#error');
const saveButton = document.querySelector('#save');
const undoButton = document.querySelector('#undo');
const editable = new Compartment();
let snapshot = { notes: [], path: '', content: '', dirty: false };
let locked = false;
let mixedLineEndings = false;
let syncFailed = false;
let missingVaultError = '';
let composing = false;

const mixedEndingsError = 'This note has mixed CRLF, LF, or CR line endings. Editing and saving are disabled to preserve its bytes (see #63).';

function lineEndings(content) {
  const separators = new Set(content.match(/\r\n|\r|\n/g) || []);
  return { mixed: separators.size > 1, separator: separators.values().next().value || '\n' };
}
let queue = Promise.resolve();

function showError(message) {
  const text = missingVaultError && message !== missingVaultError
    ? [missingVaultError, message].filter(Boolean).join('\n') : message;
  error.textContent = text;
  error.hidden = !text;
}

function showReplyError(reply, fallback) {
  showError(reply.error || (reply.ok ? '' : fallback));
}

// Serialize full-document edits before save/open/select, even when the native binding is slow.
function request(operation) {
  const result = queue.then(async () => {
    if (typeof window.inkstowRequest !== 'function') throw new Error('Native bridge is unavailable.');
    return window.inkstowRequest(operation);
  });
  queue = result.catch(() => {});
  return result.catch((failure) => ({ ok: false, error: String(failure) }));
}

function render() {
  title.textContent = snapshot.path || 'No note selected';
  dirty.hidden = !(snapshot.dirty || syncFailed || (snapshot.path && view.state.sliceDoc() !== snapshot.content));
  document.querySelector('#conflict').hidden = snapshot.change !== 'conflict';
  document.querySelector('#missing').hidden = snapshot.change !== 'missing';
  for (const button of document.querySelectorAll('.decision button')) button.disabled = locked;
  saveButton.disabled = locked || mixedLineEndings || !snapshot.path;
  undoButton.disabled = locked || mixedLineEndings || !snapshot.path;
  folder.disabled = locked;
  form.querySelector('button').disabled = locked;
  document.querySelector('#pick-vault').disabled = locked;
  document.querySelector('#create-form button').disabled = locked;
  for (const id of ['move', 'trash', 'follow', 'open-attachment']) {
    document.querySelector(`#${id}`).disabled = locked || !snapshot.path;
  }
  notes.replaceChildren(...snapshot.notes.map((path) => {
    const item = document.createElement('li');
    const button = document.createElement('button');
    button.type = 'button';
    button.textContent = path;
    button.setAttribute('aria-current', String(path === snapshot.path));
    button.disabled = locked;
    button.addEventListener('click', () => changeNote(path));
    item.append(button);
    return item;
  }));
}

function newDocument(content) {
  const endings = lineEndings(content);
  mixedLineEndings = endings.mixed;
  view.setState(EditorState.create({
    doc: content,
    extensions: [EditorState.lineSeparator.of(endings.separator), EditorState.readOnly.of(mixedLineEndings),
      history(), keymap.of([...historyKeymap, ...defaultKeymap]),
      keymap.of([{ key: 'Mod-s', run: () => { save(); return true; } }]),
      markdown(), livePreview, EditorView.domEventHandlers({
        compositionstart() { composing = true; },
        compositionend() { composing = false; },
        mousedown(event) {
          if (event.button === 0 && event.target.closest?.('.cm-preview-link')) event.preventDefault();
        },
        click(event, editor) {
          if (event.button !== 0 || locked) return false;
          const rendered = event.target.closest?.('.cm-preview-link');
          if (rendered) {
            event.preventDefault();
            followLink(rendered.dataset.kind, rendered.dataset.target);
            return true;
          }
          if (event.ctrlKey || event.metaKey) {
            const pos = editor.posAtCoords({ x: event.clientX, y: event.clientY });
            const link = pos == null ? null : sourceLinkAt(editor.state, pos);
            if (link) {
              event.preventDefault();
              followLink(link.kind, link.target);
              return true;
            }
          }
          return false;
        }
      }), keymap.of([{ key: 'Alt-Enter', run: (editor) => {
        if (composing || editor.composing) return true;
        const link = sourceLinkAt(editor.state, editor.state.selection.main.head);
        if (!link) return false;
        followLink(link.kind, link.target);
        return true;
      } }]), editable.of(EditorView.editable.of(!locked && !mixedLineEndings && Boolean(snapshot.path))),
      EditorView.updateListener.of((update) => {
        if (!update.docChanged) return;
        const text = update.state.sliceDoc();
        render();
        request({ operation: 'edit', content: text }).then((reply) => {
          if (!reply.ok) {
            syncFailed = true;
            showError(reply.error || 'Could not edit note.');
          } else if (view.state.sliceDoc() === text && snapshot.path === reply.path) {
            snapshot = reply;
            syncFailed = false;
            showError('');
          }
          render();
        });
      })]
  }));
}

const view = new EditorView({ parent: document.querySelector('#editor') });
newDocument('');
render();

function setLocked(value) {
  locked = value;
  view.dispatch({ effects: editable.reconfigure(EditorView.editable.of(!locked && !mixedLineEndings && Boolean(snapshot.path))) });
  render();
}

async function navigate(operation) {
  if (locked) return;
  setLocked(true);
  await queue;
  if (syncFailed) {
    showError('Unsaved text was not accepted by the bridge; edit it again before switching notes.');
    setLocked(false);
    return;
  }
  const reply = await request(operation);
  if (!reply.ok) {
    if (operation.operation === 'startup') missingVaultError = reply.error || 'Could not reopen last vault.';
    showError(reply.error || 'Could not open note.');
  } else {
    if (['startup', 'open', 'pickVault'].includes(operation.operation)) missingVaultError = '';
    const previousPath = snapshot.path;
    snapshot = reply;
    syncFailed = false;
    if (reply.path !== previousPath || reply.content !== view.state.sliceDoc()) newDocument(reply.content);
    showError(mixedLineEndings ? mixedEndingsError : '');
  }
  setLocked(false);
}

function changeNote(path) {
  if (path !== snapshot.path) navigate({ operation: 'select', path });
}

window.inkstowOpenPicker = () => navigate({ operation: 'pickVault' });
document.querySelector('#pick-vault').addEventListener('click', window.inkstowOpenPicker);

function followLink(kind, target) {
  if (!snapshot.path || locked) return;
  if (kind === 'external') {
    noteAction({ operation: 'external', url: target }, 'Could not follow link.');
    return;
  }
  noteAction({ operation: 'link', link: target }, 'Could not follow link.');
}

async function noteAction(operation, fallback) {
  if (locked) return;
  setLocked(true);
  await queue;
  if (syncFailed) {
    showError('Unsaved text was not accepted by the bridge; edit it again before continuing.');
    setLocked(false);
    return;
  }
  const reply = await request(operation);
  if (!reply || typeof reply.path !== 'string' || typeof reply.content !== 'string' || !Array.isArray(reply.notes)) {
    showReplyError(reply || { ok: false }, fallback);
    setLocked(false);
    return;
  }
  if (reply.path !== snapshot.path || (reply.ok && reply.content !== snapshot.content)) {
    snapshot = reply;
    syncFailed = false;
    newDocument(reply.content);
  } else {
    snapshot = reply;
  }
  showReplyError(reply, fallback);
  setLocked(false);
}

const createForm = document.querySelector('#create-form');
createForm.addEventListener('submit', (event) => {
  event.preventDefault();
  noteAction({ operation: 'create', path: document.querySelector('#new-path').value }, 'Could not create note.');
});
const moveChoice = document.querySelector('#move-choice');
const trashChoice = document.querySelector('#trash-choice');
function dismissChoices() { moveChoice.hidden = true; trashChoice.hidden = true; }
document.querySelector('#move').addEventListener('click', () => {
  dismissChoices();
  document.querySelector('#move-path').value = snapshot.path;
  moveChoice.hidden = false;
  document.querySelector('#move-path').focus();
});
document.querySelector('#trash').addEventListener('click', () => {
  dismissChoices();
  trashChoice.hidden = false;
  document.querySelector('#trash-confirm').focus();
});
document.querySelector('#move-cancel').addEventListener('click', dismissChoices);
document.querySelector('#trash-cancel').addEventListener('click', dismissChoices);
for (const [id, updateLinks] of [['move-with-links', true], ['move-without-links', false]]) {
  document.querySelector(`#${id}`).addEventListener('click', () => {
    const path = document.querySelector('#move-path').value;
    dismissChoices();
    noteAction({ operation: 'move', path, updateLinks }, 'Could not move note.');
  });
}
document.querySelector('#trash-confirm').addEventListener('click', () => {
  dismissChoices();
  noteAction({ operation: 'trash' }, 'Could not move note to trash.');
});
for (const [id, operation] of [['follow', 'follow'], ['open-attachment', 'attachment']]) {
  document.querySelector(`#${id}`).addEventListener('click', () => {
    noteAction({ operation, link: document.querySelector('#link').value }, 'Could not follow link.');
  });
}
for (const panel of [moveChoice, trashChoice]) {
  panel.addEventListener('keydown', (event) => {
    if (event.key === 'Escape') { event.preventDefault(); dismissChoices(); }
    if (event.key === 'Enter' && panel === moveChoice && event.target.id === 'move-path') {
      event.preventDefault(); document.querySelector('#move-with-links').click();
    }
  });
}

form.addEventListener('submit', (event) => {
  event.preventDefault();
  navigate({ operation: 'open', folder: folder.value });
});

async function save() {
  if (mixedLineEndings) { showError(mixedEndingsError); return; }
  if (locked || !snapshot.path) return;
  setLocked(true);
  await queue;
  if (syncFailed) {
    showError('Unsaved text was not accepted by the bridge; edit it again before saving.');
    setLocked(false);
    return;
  }
  const reply = await request({ operation: 'save' });
  if (reply.path === snapshot.path) {
    snapshot = reply;
    if (reply.change === 'reloaded') newDocument(reply.content);
  }
  showReplyError(reply, 'Could not save note.');
  setLocked(false);
  return reply.ok;
}

async function resolve(operation) {
  if (locked || !snapshot.path) return;
  setLocked(true);
  await queue;
  if (syncFailed) {
    showError('Unsaved text was not accepted by the bridge; edit it again before resolving.');
    setLocked(false);
    return;
  }
  const reply = await request({ operation });
  if (reply.path === snapshot.path) {
    snapshot = reply;
    if (reply.ok && view.state.sliceDoc() !== reply.content) newDocument(reply.content);
  }
  showReplyError(reply, 'Could not resolve outside change.');
  setLocked(false);
}

async function refresh() {
  if (locked) return;
  setLocked(true);
  await queue;
  if (!syncFailed) {
    const reply = await request({ operation: 'refresh' });
    if (reply.path === snapshot.path) {
      snapshot = reply;
      if (reply.ok && reply.change === 'reloaded') newDocument(reply.content);
    }
    showReplyError(reply, 'Could not check for outside changes.');
  }
  setLocked(false);
}

const quitPrompt = document.querySelector('#quit-prompt');
let quitAttempt = 0;
window.inkstowAskQuit = async () => {
  const attempt = quitAttempt;
  await queue;
  if (attempt !== quitAttempt) return;
  if (!syncFailed && !snapshot.dirty && view.state.sliceDoc() === snapshot.content) {
    window.inkstowExit();
    return;
  }
  quitPrompt.hidden = false;
  setLocked(true);
  document.querySelector('#quit-cancel').focus();
};
document.querySelector('#quit-cancel').addEventListener('click', () => {
  quitAttempt++;
  quitPrompt.hidden = true;
  setLocked(false);
});
document.querySelector('#quit-discard').addEventListener('click', () => window.inkstowExit());
document.querySelector('#quit-save').addEventListener('click', async () => {
  if (quitPrompt.hidden) return;
  const attempt = quitAttempt;
  setLocked(false);
  const saved = await save();
  if (attempt !== quitAttempt) return;
  if (saved && !syncFailed && !snapshot.dirty && view.state.sliceDoc() === snapshot.content) {
    window.inkstowExit();
  } else {
    setLocked(true);
    document.querySelector('#quit-cancel').focus();
  }
});

window.inkstowRefresh = refresh;
window.addEventListener('focus', refresh);
window.setInterval(() => { if (!document.hidden) refresh(); }, 3000);
for (const [id, operation] of [['reload', 'reload'], ['keep-mine', 'keepMine'], ['merge', 'merge'],
  ['save-as-new', 'saveAsNew'], ['discard', 'discard']]) {
  document.querySelector(`#${id}`).addEventListener('click', () => resolve(operation));
}
window.inkstowFollow = () => {
  if (locked || !snapshot.path || composing || view.composing) return;
  const link = sourceLinkAt(view.state, view.state.selection.main.head);
  if (link) followLink(link.kind, link.target);
  else showError('Place the cursor inside a supported link to follow it.');
};
window.inkstowSave = save;
window.inkstowUndo = () => { if (!locked) undo(view); };
saveButton.addEventListener('click', window.inkstowSave);
undoButton.addEventListener('click', window.inkstowUndo);
navigate({ operation: 'startup' });

document.addEventListener('keydown', (event) => {
  if (!quitPrompt.hidden && event.key === 'Escape') {
    event.preventDefault();
    document.querySelector('#quit-cancel').click();
    return;
  }
  if (event.ctrlKey || event.metaKey) {
    const key = event.key.toLowerCase();
    if (key === 's' || key === 'o' || key === 'q') {
      event.preventDefault();
      if (key === 's') save();
      if (key === 'o') window.inkstowOpenPicker();
      if (key === 'q') window.inkstowAskQuit();
    }
  }
});
