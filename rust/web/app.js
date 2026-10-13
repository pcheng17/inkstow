const invoke = window.__TAURI__.core.invoke;
const $ = (id) => document.getElementById(id);
let current = null;
let queue = Promise.resolve();
let conflictKind = null;

function showConflict(kind, dirty) {
  conflictKind = dirty ? kind : null;
  $('conflict').hidden = !conflictKind;
  $('conflict-message').textContent = kind === 'missing'
    ? 'This note was deleted or moved outside Inkstow. Copy any text you want to keep.'
    : 'This note changed outside Inkstow while you had unsaved edits.';
  for (const id of ['reload', 'keep', 'merge']) $(id).hidden = conflictKind !== 'conflict';
  for (const id of ['save-as-new', 'discard']) $(id).hidden = conflictKind !== 'missing';
}
function render(next, replaceText = true) {
  if (current?.path !== next.path) {
    $('move-form').hidden = true;
    $('delete-bar').hidden = true;
  }
  current = next;
  $('vault').value = next.root ?? $('vault').value;
  $('title').textContent = (next.path ?? 'No note selected') + (next.dirty ? ' ●' : '');
  $('editor').disabled = !next.path;
  if (replaceText) {
    const cursor = $('editor').selectionStart;
    $('editor').value = next.content;
    $('editor').setSelectionRange(Math.min(cursor, next.content.length), Math.min(cursor, next.content.length));
  }
  $('save').disabled = !next.path || !next.dirty;
  $('rename').disabled = !next.path || next.dirty;
  $('delete').disabled = !next.path || next.dirty;
  $('notes').replaceChildren(...next.notes.map((path) => {
    const item = document.createElement('li');
    const button = document.createElement('button');
    button.textContent = path;
    button.setAttribute('aria-current', String(path === next.path));
    button.addEventListener('click', () => run('select_note', { path }));
    item.append(button);
    return item;
  }));
}
function poll() {
  queue = queue.catch(() => {}).then(async () => {
    try {
      const { snapshot, change } = await invoke('sync_disk');
      render(snapshot, change === 'reloaded');
      if (change === 'reloaded') $('status').textContent = `Reloaded ${snapshot.path} after an outside change`;
      if (change === 'conflict') $('status').textContent = 'Warning: note changed outside Inkstow.';
      if (change === 'missing') $('status').textContent = 'Warning: note is missing or unreadable.';
      if (change === 'none' && conflictKind) $('status').textContent = 'Outside change reverted.';
      showConflict(change === 'conflict' || change === 'missing' ? change : null, snapshot.dirty);
    } catch (error) {
      $('status').textContent = `Cannot check disk: ${error}`;
    }
  });
  return queue;
}
function run(command, args = {}, replaceText = true) {
  // Serialize edits before save and polling so a response cannot replace newer typing.
  queue = queue.catch(() => {}).then(async () => {
    try {
      const result = await invoke(command, args);
      const warning = command === 'move_note' ? result.warning : null;
      render(command === 'move_note' ? result.snapshot : result, replaceText);
      if (command !== 'edit') {
        showConflict(null, false);
        if (command === 'move_note') $('move-form').hidden = true;
        if (command === 'trash_note') $('delete-bar').hidden = true;
        $('status').textContent = warning ? `Warning: ${warning}` : command === 'save' ? 'Saved.'
          : command === 'move_note' ? 'Moved note.'
          : command === 'trash_note' ? 'Moved note to trash.'
          : command === 'merge_outside' ? 'Merged outside changes; review and Save.'
          : command === 'keep_mine' ? 'Saved your version.'
          : command === 'save_as_new' ? 'Saved as new note.'
          : command === 'reload_from_disk' ? 'Reloaded from disk.' : '';
      }
    } catch (error) {
      if (command === 'save' || command === 'keep_mine' || command === 'merge_outside' || command === 'save_as_new') {
        const { snapshot, change } = await invoke('sync_disk');
        render(snapshot, change === 'reloaded');
        showConflict(change === 'conflict' || change === 'missing' ? change : null, snapshot.dirty);
      }
      $('status').textContent = `Error: ${error}`;
    }
  });
  return queue;
}
$('open').addEventListener('click', () => run('open_vault', { folder: $('vault').value }));
$('create').addEventListener('submit', (event) => {
  event.preventDefault();
  run('create_note', { path: $('new-path').value });
});
$('rename').addEventListener('click', () => {
  if (!current?.path || current.dirty) return;
  $('delete-bar').hidden = true;
  $('move-path').value = current.path;
  $('update-links').checked = true;
  $('move-warning').hidden = true;
  $('move-form').hidden = false;
  $('move-path').focus();
});
$('cancel-move').addEventListener('click', () => { $('move-form').hidden = true; });
$('update-links').addEventListener('change', () => {
  $('move-warning').hidden = $('update-links').checked;
});
$('move-form').addEventListener('submit', (event) => {
  event.preventDefault();
  run('move_note', { path: $('move-path').value, updateLinks: $('update-links').checked });
});
$('delete').addEventListener('click', () => {
  if (!current?.path || current.dirty) return;
  $('move-form').hidden = true;
  $('delete-bar').hidden = false;
});
$('cancel-delete').addEventListener('click', () => { $('delete-bar').hidden = true; });
$('confirm-delete').addEventListener('click', () => run('trash_note'));
$('save').addEventListener('click', () => run('save', {}, false));
$('editor').addEventListener('input', () => run('edit', { content: $('editor').value }, false));
for (const [id, command, replace] of [
  ['reload', 'reload_from_disk', true], ['keep', 'keep_mine', false],
  ['merge', 'merge_outside', true], ['save-as-new', 'save_as_new', false],
  ['discard', 'discard_edits', true]
]) $(id).addEventListener('click', () => run(command, {}, replace));
document.addEventListener('keydown', (event) => {
  if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 's') {
    event.preventDefault();
    run('save', {}, false);
  }
  if (event.key === 'F2' && current?.path && !current.dirty) {
    event.preventDefault();
    $('rename').click();
  }
});
// Polling also catches atomic file replacement, which can drop a file watcher.
// Throttle while the window is hidden; recheck immediately on return.
setInterval(() => { if (!document.hidden) poll(); }, 900);
document.addEventListener('visibilitychange', () => { if (!document.hidden) poll(); });
window.addEventListener('focus', poll);
run('view');
