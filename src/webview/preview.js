import { syntaxTree } from '@codemirror/language';
import { Decoration, ViewPlugin, WidgetType } from '@codemirror/view';

const hidden = Decoration.replace({});
const heading = Decoration.line({ class: 'cm-preview-heading' });
const quote = Decoration.line({ class: 'cm-preview-quote' });
const list = Decoration.line({ class: 'cm-preview-list' });
const strong = Decoration.mark({ class: 'cm-strong' });
const emphasis = Decoration.mark({ class: 'cm-emphasis' });
const code = Decoration.mark({ class: 'cm-inline-code' });

class ListBullet extends WidgetType {
  constructor(label) { super(); this.label = label; }
  eq(other) { return this.label === other.label; }
  toDOM(view) {
    const span = view.dom.ownerDocument.createElement('span');
    span.className = 'cm-preview-bullet';
    span.textContent = this.label;
    return span;
  }
}

// Return only links whose complete source includes the cursor. Invalid partial markup stays text.
export function sourceLinkAt(state, pos) {
  for (let node = syntaxTree(state).resolveInner(pos); node; node = node.parent) {
    if (['InlineCode', 'FencedCode', 'CodeBlock'].includes(node.name)) return null;
  }
  const line = state.doc.lineAt(pos);
  for (const match of line.text.matchAll(/\[\[([^\[\]\r\n]+)\]\]|\[([^\]\r\n]+)\]\(([^\s()]+)\)/g)) {
    const from = line.from + match.index;
    if (pos < from || pos > from + match[0].length) continue;
    if (match[1]) {
      const target = match[1].split('|', 1)[0];
      if (target) return { kind: 'wiki', target };
    } else if (!match[3].includes(':') || /^https?:\/\//.test(match[3])) {
      return { kind: /^https?:\/\//.test(match[3]) ? 'external' : 'wiki', target: match[3] };
    }
  }
  return null;
}

function active(view, from, to) {
  return view.state.selection.ranges.some((range) => {
    const first = view.state.doc.lineAt(range.from).number;
    const last = view.state.doc.lineAt(range.to).number;
    const start = view.state.doc.lineAt(from).number;
    const end = view.state.doc.lineAt(to).number;
    return first <= end && start <= last;
  });
}

export function preview(view) {
  const decorations = [];
  const add = (from, to, decoration) => {
    if (from < to) decorations.push(decoration.range(from, to));
  };
  const addLine = (from, decoration) => decorations.push(decoration.range(from));
  const source = view.state.doc.toString();
  // Wikilinks aren't part of the standard Markdown grammar. Only complete, single-line
  // links outside inline/fenced code are presented as links.
  const wikiRanges = [];
  for (const match of source.matchAll(/\[\[([^\[\]\r\n]+)\]\]/g)) {
    const from = match.index;
    const to = from + match[0].length;
    const parent = syntaxTree(view.state).resolveInner(from + 2);
    if (['InlineCode', 'FencedCode', 'CodeBlock'].some((name) => {
      for (let node = parent; node; node = node.parent) if (node.name === name) return true;
      return false;
    })) continue;
    wikiRanges.push({ from, to });
    if (active(view, from, to)) continue;
    const [target, alias] = match[1].split('|', 2);
    if (!target) continue;
    const labelFrom = alias ? from + 2 + target.length + 1 : from + 2;
    add(from, labelFrom, hidden);
    add(labelFrom, to - 2, Decoration.mark({ class: 'cm-preview-link', attributes: { 'data-kind': 'wiki', 'data-target': target } }));
    add(to - 2, to, hidden);
  }
  syntaxTree(view.state).iterate({ enter(node) {
    if (wikiRanges.some((range) => node.from >= range.from && node.to <= range.to)) return false;
    if (active(view, node.from, node.to) ||
        /^(<<<<<<< |=======|>>>>>>> )/.test(view.state.doc.lineAt(node.from).text)) return;
    if (/^ATXHeading[1-6]$/.test(node.name)) addLine(node.from, heading);
    if (node.name === 'Blockquote') addLine(node.from, quote);
    if (node.name === 'ListItem') addLine(node.from, list);
    if (node.name === 'StrongEmphasis') add(node.from, node.to, strong);
    if (node.name === 'Emphasis') add(node.from, node.to, emphasis);
    if (node.name === 'InlineCode') add(node.from, node.to, code);
    if (node.name === 'ListMark') {
      const marker = source.slice(node.from, node.to);
      add(node.from, node.to, Decoration.replace({ widget: new ListBullet(/^\d/.test(marker) ? marker : '•') }));
      return;
    }
    if (['HeaderMark', 'EmphasisMark', 'CodeMark', 'QuoteMark'].includes(node.name)) {
      add(node.from, node.to, hidden);
      return;
    }
    if (node.name === 'Link') {
      const text = source.slice(node.from, node.to);
      const match = /^\[([^\]\r\n]+)\]\(([^\s()]+)\)$/.exec(text);
      if (match && (!match[2].includes(':') || /^https?:\/\//.test(match[2]))) {
        const labelEnd = node.from + 1 + match[1].length;
        add(node.from, node.from + 1, hidden);
        add(node.from + 1, labelEnd, Decoration.mark({ class: 'cm-preview-link', attributes: {
          'data-kind': /^https?:\/\//.test(match[2]) ? 'external' : 'wiki', 'data-target': match[2]
        } }));
        add(labelEnd, node.to, hidden);
        return false;
      }
    }
  } });
  decorations.sort((a, b) => a.from - b.from || a.startSide - b.startSide);
  return Decoration.set(decorations, true);
}

export const livePreview = ViewPlugin.fromClass(class {
  constructor(view) { this.decorations = preview(view); }
  update(update) {
    if (update.docChanged || update.selectionSet || update.viewportChanged)
      this.decorations = preview(update.view);
  }
}, { decorations: (plugin) => plugin.decorations });
