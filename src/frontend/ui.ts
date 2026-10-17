const notes = document.querySelector('#notes');
const error = document.querySelector('#error');

function setBoolean(element, property, value) {
  if (element[property] !== value) element[property] = value;
}

export function showError(message, missingVaultError) {
  const text = missingVaultError && message !== missingVaultError
    ? [missingVaultError, message].filter(Boolean).join('\n') : message;
  if (error.textContent !== text) error.textContent = text;
  if (error.hidden !== !text) error.hidden = !text;
}

function syncNotes(list, branch, selectedPath, controlsLocked, changeNote) {
  const existing = new Map([...list.children].map((item) => [item.dataset.key, item]));
  const entries = [
    ...[...branch.folders].sort(([a], [b]) => a.localeCompare(b)).map(([name, child]) => ({ name, child })),
    ...branch.files.sort().map((path) => ({ path })),
  ];
  const keys = new Set(entries.map((entry) => entry.child ? `folder:${entry.child.path}` : `note:${entry.path}`));
  for (const [key, item] of existing) if (!keys.has(key)) { item.remove(); existing.delete(key); }
  entries.forEach((entry, index) => {
    const key = entry.child ? `folder:${entry.child.path}` : `note:${entry.path}`;
    let item = existing.get(key);
    if (!item) {
      item = document.createElement('li');
      item.dataset.key = key;
      if (entry.child) {
        const details = document.createElement('details');
        details.open = true;
        const summary = document.createElement('summary');
        const icon = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
        icon.setAttribute('viewBox', '0 0 24 24');
        icon.setAttribute('aria-hidden', 'true');
        const shape = document.createElementNS('http://www.w3.org/2000/svg', 'path');
        shape.setAttribute('d', 'M3 5h6l2 2h10v12H3z');
        icon.append(shape);
        summary.append(icon, document.createTextNode(entry.name));
        const children = document.createElement('ul');
        details.append(summary, children);
        item.append(details);
      } else {
        const button = document.createElement('button');
        button.type = 'button';
        button.textContent = entry.path.split('/').at(-1);
        button.setAttribute('aria-label', entry.path);
        button.title = entry.path;
        button.addEventListener('click', () => changeNote(entry.path));
        item.append(button);
      }
    }
    if (entry.child) {
      syncNotes(item.firstElementChild.lastElementChild, entry.child, selectedPath, controlsLocked, changeNote);
    } else {
      const button = item.firstElementChild;
      const selected = String(entry.path === selectedPath);
      if (button.getAttribute('aria-current') !== selected) button.setAttribute('aria-current', selected);
      setBoolean(button, 'disabled', controlsLocked);
    }
    if (list.children[index] !== item) list.insertBefore(item, list.children[index] || null);
  });
}

function renderNotes(paths, selectedPath, controlsLocked, changeNote) {
  const root = { folders: new Map(), files: [], path: '' };
  for (const path of paths) {
    const parts = path.split('/');
    let branch = root;
    for (const name of parts.slice(0, -1)) {
      if (!branch.folders.has(name)) {
        branch.folders.set(name, { folders: new Map(), files: [], path: branch.path ? `${branch.path}/${name}` : name });
      }
      branch = branch.folders.get(name);
    }
    branch.files.push(path);
  }
  const previous = notes.dataset.selectedPath || '';
  syncNotes(notes, root, selectedPath, controlsLocked, changeNote);
  // A selection made via a link or move must be visible even if its folder was collapsed.
  if (selectedPath && selectedPath !== previous) {
    const selected = [...notes.querySelectorAll('button')].find((button) => button.title === selectedPath);
    for (let parent = selected?.parentElement; parent && parent !== notes; parent = parent.parentElement) {
      if (parent.tagName === 'DETAILS') parent.open = true;
    }
  }
  if (previous !== selectedPath) notes.dataset.selectedPath = selectedPath;
}

export function render({ snapshot, controlsLocked, syncFailed, content, changeNote }) {
  setBoolean(document.querySelector('#empty-vault'), 'hidden', snapshot.vaultOpen);
  setBoolean(document.querySelector('main'), 'hidden', !snapshot.vaultOpen);
  const label = snapshot.path || 'No note selected';
  const title = document.querySelector('#title');
  if (title.textContent !== label) title.textContent = label;
  setBoolean(document.querySelector('#dirty'), 'hidden', !(snapshot.dirty || syncFailed ||
    (snapshot.path && content !== snapshot.content)));
  setBoolean(document.querySelector('#conflict'), 'hidden', snapshot.change !== 'conflict');
  setBoolean(document.querySelector('#missing'), 'hidden', snapshot.change !== 'missing');
  for (const button of document.querySelectorAll('.decision button')) setBoolean(button, 'disabled', controlsLocked);
  setBoolean(document.querySelector('#pick-vault'), 'disabled', controlsLocked);
  setBoolean(document.querySelector('#create-form button'), 'disabled', controlsLocked);
  for (const id of ['move', 'trash']) {
    setBoolean(document.querySelector(`#${id}`), 'disabled', controlsLocked || !snapshot.path);
  }
  renderNotes(snapshot.notes, snapshot.path, controlsLocked, changeNote);
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
  for (const panel of [moveChoice, trashChoice]) {
    panel.addEventListener('keydown', (event) => {
      if (event.key === 'Escape') { event.preventDefault(); dismissChoices(); }
      if (event.key === 'Enter' && panel === moveChoice && event.target.id === 'move-path') {
        event.preventDefault(); document.querySelector('#move-with-links').click();
      }
    });
  }
}
