import { EditorState, Compartment } from '@codemirror/state';
import { EditorView, keymap } from '@codemirror/view';
import { defaultKeymap, history, historyKeymap, undo } from '@codemirror/commands';
import { markdown } from '@codemirror/lang-markdown';
import { livePreview, sourceLinkAt } from './preview.js';

export function createNoteEditor({ save, followLink, onEdit, onChange, canFollow, selectedPath }) {
  const editable = new Compartment();
  let mixedLineEndings = false;
  let locked = false;
  let composing = false;
  let view;

  function lineEndings(content) {
    const separators = new Set(content.match(/\r\n|\r|\n/g) || []);
    return { mixed: separators.size > 1, separator: separators.values().next().value || '\n' };
  }

  function load(content) {
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
        } }]), editable.of(EditorView.editable.of(!locked && !mixedLineEndings && Boolean(selectedPath()))),
        EditorView.updateListener.of((update) => {
          if (!update.docChanged) return;
          const text = update.state.sliceDoc();
          onChange();
          onEdit(text);
        })]
    }));
  }

  view = new EditorView({ parent: document.querySelector('#editor') });
  load('');
  return {
    load,
    get content() { return view.state.sliceDoc(); },
    get mixedLineEndings() { return mixedLineEndings; },
    setLocked(value) {
      locked = value;
      view.dispatch({ effects: editable.reconfigure(EditorView.editable.of(!locked && !mixedLineEndings && Boolean(selectedPath()))) });
    },
    followAtCursor() {
      if (!canFollow() || composing || view.composing) return false;
      const link = sourceLinkAt(view.state, view.state.selection.main.head);
      if (!link) return false;
      followLink(link.kind, link.target);
      return true;
    },
    undo() { if (!locked) undo(view); }
  };
}
