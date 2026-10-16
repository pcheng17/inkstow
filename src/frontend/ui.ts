const folder = document.querySelector('#folder');
const form = document.querySelector('#vault-form');
const notes = document.querySelector('#notes');
const error = document.querySelector('#error');
const saveButton = document.querySelector('#save');
const undoButton = document.querySelector('#undo');

function setBoolean(element, property, value) {
  if (element[property] !== value) element[property] = value;
}

export function showError(message, missingVaultError) {
  const text = missingVaultError && message !== missingVaultError
    ? [missingVaultError, message].filter(Boolean).join('\n') : message;
  if (error.textContent !== text) error.textContent = text;
  if (error.hidden !== !text) error.hidden = !text;
}

export function render({ snapshot, controlsLocked, mixedLineEndings, syncFailed, content, changeNote }) {
  const label = snapshot.path || 'No note selected';
  const title = document.querySelector('#title');
  if (title.textContent !== label) title.textContent = label;
  setBoolean(document.querySelector('#dirty'), 'hidden', !(snapshot.dirty || syncFailed ||
    (snapshot.path && content !== snapshot.content)));
  setBoolean(document.querySelector('#conflict'), 'hidden', snapshot.change !== 'conflict');
  setBoolean(document.querySelector('#missing'), 'hidden', snapshot.change !== 'missing');
  for (const button of document.querySelectorAll('.decision button')) setBoolean(button, 'disabled', controlsLocked);
  setBoolean(saveButton, 'disabled', controlsLocked || mixedLineEndings || !snapshot.path);
  setBoolean(undoButton, 'disabled', controlsLocked || mixedLineEndings || !snapshot.path);
  setBoolean(folder, 'disabled', controlsLocked);
  setBoolean(form.querySelector('button'), 'disabled', controlsLocked);
  setBoolean(document.querySelector('#pick-vault'), 'disabled', controlsLocked);
  setBoolean(document.querySelector('#create-form button'), 'disabled', controlsLocked);
  for (const id of ['move', 'trash', 'follow', 'open-attachment']) {
    setBoolean(document.querySelector(`#${id}`), 'disabled', controlsLocked || !snapshot.path);
  }
  const existing = new Map([...notes.children].map((item) => [item.firstElementChild.textContent, item]));
  const paths = new Set(snapshot.notes);
  for (const [path, item] of existing) if (!paths.has(path)) item.remove();
  snapshot.notes.forEach((path, index) => {
    let item = existing.get(path);
    if (!item) {
      item = document.createElement('li');
      const button = document.createElement('button');
      button.type = 'button';
      button.textContent = path;
      button.addEventListener('click', () => changeNote(path));
      item.append(button);
    }
    const button = item.firstElementChild;
    const selected = String(path === snapshot.path);
    if (button.getAttribute('aria-current') !== selected) button.setAttribute('aria-current', selected);
    setBoolean(button, 'disabled', controlsLocked);
    if (notes.children[index] !== item) notes.insertBefore(item, notes.children[index] || null);
  });
}

export function bindNoteControls({ isLocked, selectedPath, noteAction }) {
  document.querySelector('#create-form').addEventListener('submit', (event) => {
    event.preventDefault();
    noteAction({ operation: 'create', path: document.querySelector('#new-path').value }, 'Could not create note.');
  });
  const moveChoice = document.querySelector('#move-choice');
  const trashChoice = document.querySelector('#trash-choice');
  function dismissChoices() { moveChoice.hidden = true; trashChoice.hidden = true; }
  document.querySelector('#move').addEventListener('click', () => {
    if (isLocked()) return;
    dismissChoices();
    document.querySelector('#move-path').value = selectedPath();
    moveChoice.hidden = false;
    document.querySelector('#move-path').focus();
  });
  document.querySelector('#trash').addEventListener('click', () => {
    if (isLocked()) return;
    dismissChoices();
    trashChoice.hidden = false;
    document.querySelector('#trash-confirm').focus();
  });
  document.querySelector('#move-cancel').addEventListener('click', dismissChoices);
  document.querySelector('#trash-cancel').addEventListener('click', dismissChoices);
  for (const [id, updateLinks] of [['move-with-links', true], ['move-without-links', false]]) {
    document.querySelector(`#${id}`).addEventListener('click', () => {
      if (isLocked()) return;
      const path = document.querySelector('#move-path').value;
      dismissChoices();
      noteAction({ operation: 'move', path, updateLinks }, 'Could not move note.');
    });
  }
  document.querySelector('#trash-confirm').addEventListener('click', () => {
    if (isLocked()) return;
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
}
