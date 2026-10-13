// Follow the [[link]] at the caret like the Qt editor: Alt+Return (Option+Return on
// macOS), or Ctrl+click (Cmd+click on macOS) that leaves no selection. Carets are
// UTF-16 offsets, which the core expects.
function installFollowLink(editor, follow) {
  const mac = /Mac|iPhone|iPad/.test(navigator.platform);
  const caret = () => (editor.selectionDirection === 'backward' ? editor.selectionStart : editor.selectionEnd);
  editor.addEventListener('keydown', (event) => {
    if (event.key !== 'Enter' || !event.altKey || event.ctrlKey || event.metaKey || event.shiftKey) return;
    event.preventDefault();
    follow(caret());
  });
  editor.addEventListener('click', (event) => {
    if (event.button !== 0 || !(mac ? event.metaKey : event.ctrlKey)) return;
    if (editor.selectionStart === editor.selectionEnd) follow(caret());
  });
  return () => follow(caret());
}
