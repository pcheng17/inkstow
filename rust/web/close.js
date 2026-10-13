// Quit protection. Rust cancels every window close and app quit, then emits
// `close-requested`; the core's CloseGuard decides whether to exit or prompt.
// `enqueue` runs a task after queued edits so the decision sees the latest text.
function installQuitGuard({ enqueue, editorText, onSaveFailed }) {
  const { invoke } = window.__TAURI__.core;
  const prompt = document.getElementById('quit-prompt');
  const decide = (action) => enqueue(async () => {
    try {
      const outcome = await invoke('close_app', { action, editor: editorText() });
      if (outcome === 'prompt') {
        prompt.showModal();
        document.getElementById('cancel-quit').focus();
      } else if (outcome === 'stay') {
        prompt.close();
      }
    } catch (error) {
      // A failed Save and quit keeps the window and edits; show why.
      prompt.close();
      onSaveFailed(error);
    }
  });
  // A reloaded page has no open prompt, so clear any the previous page left in Rust.
  // No editor text: the editor is still empty until the first view.
  enqueue(() => invoke('close_app', { action: 'cancel', editor: null }).catch(() => {}));
  window.__TAURI__.event.listen('close-requested', () => decide('request'));
  // Escape cancels, like the Cancel button, so the guard can prompt again later.
  prompt.addEventListener('cancel', (event) => {
    event.preventDefault();
    decide('cancel');
  });
  for (const [id, action] of [['cancel-quit', 'cancel'], ['discard-quit', 'discard'], ['save-quit', 'save']]) {
    document.getElementById(id).addEventListener('click', () => decide(action));
  }
}
