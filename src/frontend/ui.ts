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
        icon.setAttribute('viewBox', '0 0 16 16');
        icon.setAttribute('aria-hidden', 'true');
        const chevron = document.createElementNS('http://www.w3.org/2000/svg', 'path');
        chevron.setAttribute('d', 'M6 3.5 10.5 8 6 12.5');
        icon.append(chevron);
        summary.append(icon, document.createTextNode(entry.name));
        const children = document.createElement('ul');
        details.append(summary, children);
        item.append(details);
      } else {
        const button = document.createElement('button');
        button.type = 'button';
        button.textContent = entry.path.split('/').at(-1);
        button.setAttribute('aria-label', entry.path);
        button.setAttribute('aria-haspopup', 'menu');
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
  setBoolean(document.querySelector('#trash'), 'disabled', controlsLocked || !snapshot.path);
  const menu = document.querySelector('#note-menu');
  if (!menu.hidden && !snapshot.notes.includes(menu.dataset.path)) menu.hidden = true;
  const renameChoice = document.querySelector('#rename-choice');
  if (!renameChoice.hidden && renameChoice.dataset.path !== snapshot.path) renameChoice.hidden = true;
  renderNotes(snapshot.notes, snapshot.path, controlsLocked, changeNote);
}

export function bindNoteControls({ isLocked, selectedPath, selectNote, noteAction, showError }) {
  document.querySelector('#create-form').addEventListener('submit', (event) => {
    event.preventDefault();
    noteAction({ operation: 'create', path: document.querySelector('#new-path').value }, 'Could not create note.');
  });
  const renameChoice = document.querySelector('#rename-choice');
  const trashChoice = document.querySelector('#trash-choice');
  const menu = document.querySelector('#note-menu');
  const noteList = document.querySelector('#notes');
  function dismissChoices() { renameChoice.hidden = true; trashChoice.hidden = true; }
  function dismissMenu(restoreFocus = false) {
    if (!menu.hidden) menu.hidden = true;
    if (restoreFocus) [...noteList.querySelectorAll('button')]
      .find((button) => button.title === menu.dataset.path)?.focus();
  }
  function showMenu(button, x, y) {
    if (isLocked() || button.disabled) return;
    menu.dataset.path = button.title;
    menu.setAttribute('aria-label', `Note actions for ${button.title}`);
    menu.hidden = false;
    menu.style.left = `${Math.max(0, Math.min(x, window.innerWidth - menu.offsetWidth - 8))}px`;
    menu.style.top = `${Math.max(0, Math.min(y, window.innerHeight - menu.offsetHeight - 8))}px`;
    document.querySelector('#note-menu-rename').focus();
  }
  noteList.addEventListener('contextmenu', (event) => {
    const button = event.target.closest('button');
    if (!button || !noteList.contains(button)) return;
    event.preventDefault();
    showMenu(button, event.clientX, event.clientY);
  });
  noteList.addEventListener('keydown', (event) => {
    if (event.key !== 'ContextMenu' && !(event.key === 'F10' && event.shiftKey)) return;
    const button = event.target.closest('button');
    if (!button || !noteList.contains(button)) return;
    event.preventDefault();
    const rect = button.getBoundingClientRect();
    showMenu(button, rect.left, rect.bottom);
  });
  document.addEventListener('click', (event) => {
    if (!menu.contains(event.target)) dismissMenu();
  });
  menu.addEventListener('keydown', (event) => {
    if (event.key === 'Escape') { event.preventDefault(); dismissMenu(true); }
  });
  document.querySelector('#note-menu-rename').addEventListener('click', async () => {
    const path = menu.dataset.path;
    dismissMenu();
    if (isLocked()) return;
    if (path !== selectedPath()) await selectNote(path);
    if (isLocked() || selectedPath() !== path) { dismissMenu(true); return; }
    dismissChoices();
    renameChoice.dataset.path = path;
    document.querySelector('#rename-name').value = path.split('/').at(-1);
    renameChoice.hidden = false;
    document.querySelector('#rename-name').focus();
  });
  document.querySelector('#trash').addEventListener('click', () => {
    if (isLocked()) return;
    dismissChoices();
    trashChoice.hidden = false;
    document.querySelector('#trash-confirm').focus();
  });
  document.querySelector('#rename-cancel').addEventListener('click', dismissChoices);
  document.querySelector('#trash-cancel').addEventListener('click', dismissChoices);
  for (const [id, updateLinks] of [['rename-with-links', true], ['rename-without-links', false]]) {
    document.querySelector(`#${id}`).addEventListener('click', () => {
      if (isLocked()) return;
      const original = renameChoice.dataset.path;
      if (selectedPath() !== original) { dismissChoices(); return; }
      const name = document.querySelector('#rename-name').value.trim();
      if (!name || name.startsWith('.') || /[/\\]/.test(name)) {
        showError('Enter a note name without folders or path separators.');
        document.querySelector('#rename-name').focus();
        return;
      }
      const path = original.slice(0, original.lastIndexOf('/') + 1) + name;
      dismissChoices();
      noteAction({ operation: 'move', path, updateLinks }, 'Could not rename note.');
    });
  }
  document.querySelector('#trash-confirm').addEventListener('click', () => {
    if (isLocked()) return;
    dismissChoices();
    noteAction({ operation: 'trash' }, 'Could not move note to trash.');
  });
  for (const panel of [renameChoice, trashChoice]) {
    panel.addEventListener('keydown', (event) => {
      if (event.key === 'Escape') { event.preventDefault(); dismissChoices(); }
      if (event.key === 'Enter' && panel === renameChoice && event.target.id === 'rename-name') {
        event.preventDefault(); document.querySelector('#rename-with-links').click();
      }
    });
  }
  return dismissMenu;
}
