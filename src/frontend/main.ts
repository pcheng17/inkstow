import { createNoteEditor } from './editor.js';
import { render as renderUi, showError as showUiError, bindNoteControls } from './ui.js';

const folder = document.querySelector('#folder');
const form = document.querySelector('#vault-form');
const busy = document.querySelector('#busy');
const quitPrompt = document.querySelector('#quit-prompt');
const saveButton = document.querySelector('#save');
const undoButton = document.querySelector('#undo');
let snapshot = { notes: [], path: '', content: '', dirty: false };
let locked = false;
let controlsLocked = false;
let syncFailed = false;
let missingVaultError = '';
let busyTimer;
let refreshing = false;
let pendingKeepMine = null;

const mixedEndingsError = 'This note has mixed CRLF, LF, or CR line endings. Editing and saving are disabled to preserve its bytes (see #63).';

let queue = Promise.resolve();

function showError(message) { showUiError(message, missingVaultError); }

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
  renderUi({ snapshot, controlsLocked, mixedLineEndings: editor.mixedLineEndings,
    syncFailed, content: editor.content, changeNote });
}

const editor = createNoteEditor({
  save: () => save(), followLink: (kind, target) => followLink(kind, target),
  selectedPath: () => snapshot.path,
  onChange: render,
  onEdit: (text) => {
    request({ operation: 'edit', content: text }).then((reply) => {
      if (!reply.ok) {
        syncFailed = true;
        showError(reply.error || 'Could not edit note.');
      } else if (editor.content === text && snapshot.path === reply.path) {
        snapshot = reply;
        syncFailed = false;
        showError('');
      }
      render();
    });
  }
});
render();

function setLocked(value, updateUi = true) {
  locked = value;
  // Only modal locks visually disable controls. Bridge work blocks competing actions
  // and editing without making every button flash gray on an unrelated render().
  controlsLocked = value && updateUi;
  if (!value) {
    window.clearTimeout(busyTimer);
    if (busy.textContent) busy.textContent = '';
  }
  editor.setLocked(value);
  if (updateUi) render();
}

function lockForAction() {
  setLocked(true, false);
  // Fast bridge replies need no indicator; slow ones should explain why clicks wait.
  busyTimer = window.setTimeout(() => { if (locked) busy.textContent = 'Working…'; }, 150);
}

async function navigate(operation) {
  if (locked) return;
  // Ignore competing actions and pause editor input without dimming every control.
  // The new note replaces the visible one only after the bridge replies.
  if (operation.operation === 'startup') setLocked(true, false);
  else lockForAction();
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
    if (reply.path !== previousPath || reply.content !== editor.content) editor.load(reply.content);
    showError(editor.mixedLineEndings ? mixedEndingsError : '');
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
  lockForAction();
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
    editor.load(reply.content);
  } else {
    snapshot = reply;
  }
  showReplyError(reply, fallback);
  setLocked(false);
}

bindNoteControls({ isLocked: () => locked, selectedPath: () => snapshot.path, noteAction });

form.addEventListener('submit', (event) => {
  event.preventDefault();
  navigate({ operation: 'open', folder: folder.value });
});

async function save() {
  if (editor.mixedLineEndings) { showError(mixedEndingsError); return; }
  if (locked || !snapshot.path) return;
  if (quitPrompt.hidden) lockForAction();
  else setLocked(true); // Keep the modal lock while Save and quit is in flight.
  await queue;
  if (syncFailed) {
    showError('Unsaved text was not accepted by the bridge; edit it again before saving.');
    setLocked(false);
    return;
  }
  const reply = await request({ operation: 'save' });
  if (reply.path === snapshot.path) {
    snapshot = reply;
    if (reply.change === 'reloaded') editor.load(reply.content);
  }
  showReplyError(reply, 'Could not save note.');
  setLocked(false);
  return reply.ok;
}

async function resolve(operation) {
  if (locked) {
    if (operation === 'keepMine' && refreshing && quitPrompt.hidden && !pendingKeepMine &&
        snapshot.path && snapshot.change === 'conflict') {
      pendingKeepMine = { path: snapshot.path, conflictRevision: snapshot.conflictRevision };
    }
    return;
  }
  if (!snapshot.path) return;
  lockForAction();
  await queue;
  if (syncFailed) {
    showError('Unsaved text was not accepted by the bridge; edit it again before resolving.');
    setLocked(false);
    return;
  }
  const reply = await request({ operation });
  if (reply.path === snapshot.path) {
    snapshot = reply;
    if (reply.ok && editor.content !== reply.content) editor.load(reply.content);
  }
  showReplyError(reply, 'Could not resolve outside change.');
  setLocked(false);
}

async function refresh() {
  if (locked) return;
  // Keep edits blocked while the bridge checks disk, but don't disable and redraw the
  // entire UI for a background refresh that usually has nothing new to show.
  setLocked(true, false);
  refreshing = true;
  await queue;
  let refreshOk = false;
  if (!syncFailed) {
    const reply = await request({ operation: 'refresh' });
    refreshOk = reply.ok;
    if (reply.path === snapshot.path) {
      snapshot = reply;
      if (reply.ok && reply.change === 'reloaded') editor.load(reply.content);
    }
    showReplyError(reply, 'Could not check for outside changes.');
  }
  refreshing = false;
  if (quitPrompt.hidden) setLocked(false, false);
  render();
  const decision = pendingKeepMine;
  pendingKeepMine = null;
  if (!decision || !refreshOk || !quitPrompt.hidden || controlsLocked || syncFailed ||
      snapshot.path !== decision.path || snapshot.change !== 'conflict') return;
  if (snapshot.conflictRevision !== decision.conflictRevision) {
    showError('Outside note changed again; review the new conflict before choosing Keep mine.');
    return;
  }
  resolve('keepMine');
}

let quitAttempt = 0;
window.inkstowAskQuit = async () => {
  const attempt = quitAttempt;
  await queue;
  if (attempt !== quitAttempt) return;
  if (!syncFailed && !snapshot.dirty && editor.content === snapshot.content) {
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
  setLocked(false, false); // Let save run without briefly re-enabling controls behind the prompt.
  const saved = await save();
  if (attempt !== quitAttempt) return;
  if (saved && !syncFailed && !snapshot.dirty && editor.content === snapshot.content) {
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
  if (locked || !snapshot.path) return;
  if (editor.followAtCursor() === false) showError('Place the cursor inside a supported link to follow it.');
};
window.inkstowSave = save;
window.inkstowUndo = () => editor.undo();
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
