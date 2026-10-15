import { syntaxTree } from '@codemirror/language';
import { Decoration, ViewPlugin, WidgetType } from '@codemirror/view';

const hidden = Decoration.replace({});
const heading = Decoration.line({ class: 'cm-preview-heading' });
const quote = Decoration.line({ class: 'cm-preview-quote' });
const list = Decoration.line({ class: 'cm-preview-list' });
const strong = Decoration.mark({ class: 'cm-strong' });
const emphasis = Decoration.mark({ class: 'cm-emphasis' });
const code = Decoration.mark({ class: 'cm-inline-code' });
const blockCode = Decoration.line({ class: 'cm-block-code' });

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

function linkKind(target) {
  if (/^https?:\/\//.test(target)) return 'external';
  return target.includes(':') ? null : 'wiki';
}

function escaped(text, from) {
  let slashes = 0;
  for (let index = from - 1; index >= 0 && text[index] === '\\'; index--) slashes++;
  return slashes % 2 === 1;
}

function markdownLink(source, node) {
  if (node.name !== 'Link') return null;
  const url = node.getChild('URL');
  if (!url) return null; // Reference links have no inline destination.
  let labelEnd;
  for (let child = node.firstChild; child; child = child.nextSibling) {
    if (child.name === 'LinkMark' && source.slice(child.from, child.to) === ']') {
      labelEnd = child.from;
      break;
    }
  }
  if (labelEnd === undefined) return null;
  const raw = source.slice(url.from, url.to);
  const destination = raw.startsWith('<') && raw.endsWith('>') ? raw.slice(1, -1) : raw;
  // Markdown escapes ASCII punctuation in destinations; the vault expects literal paths.
  const target = destination.replace(/\\([!-/:-@\[-`{-~])/g, '$1');
  const kind = target && linkKind(target);
  return kind ? { kind, target, labelEnd } : null;
}

function inCode(state, pos) {
  for (let node = syntaxTree(state).resolveInner(pos); node; node = node.parent) {
    if (['InlineCode', 'FencedCode', 'CodeBlock'].includes(node.name)) return true;
  }
  return false;
}

// Return only links whose complete source includes the cursor. Invalid partial markup stays text.
export function sourceLinkAt(state, pos) {
  if (inCode(state, pos)) return null;
  const line = state.doc.lineAt(pos);
  for (const match of line.text.matchAll(/\[\[([^\[\]\r\n]+)\]\]/g)) {
    const from = line.from + match.index;
    const to = from + match[0].length;
    if (pos < from || pos > to || escaped(line.text, match.index)) continue;
    const [target, alias, extra] = match[1].split('|');
    if (target && extra === undefined && (alias === undefined || alias))
      return { kind: 'wiki', target };
  }
  const source = state.doc.toString();
  for (const side of [-1, 1]) {
    for (let node = syntaxTree(state).resolveInner(pos, side); node; node = node.parent) {
      if (node.from > pos || node.to < pos) continue;
      const link = markdownLink(source, node);
      if (link) return { kind: link.kind, target: link.target };
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
    if (escaped(source, from) || inCode(view.state, from + 2)) continue;
    const [target, alias, extra] = match[1].split('|');
    if (!target || extra !== undefined || (alias !== undefined && !alias)) continue;
    wikiRanges.push({ from, to });
    if (active(view, from, to)) continue;
    const labelFrom = alias ? from + 2 + target.length + 1 : from + 2;
    add(from, labelFrom, hidden);
    add(labelFrom, to - 2, Decoration.mark({ class: 'cm-preview-link', attributes: { 'data-kind': 'wiki', 'data-target': target } }));
    add(to - 2, to, hidden);
  }
  syntaxTree(view.state).iterate({ enter(node) {
    if (wikiRanges.some((range) => node.from >= range.from && node.to <= range.to)) return false;
    if (node.name === 'FencedCode' || node.name === 'CodeBlock') {
      const first = view.state.doc.lineAt(node.from).number;
      const last = view.state.doc.lineAt(node.to).number;
      for (let number = first; number <= last; number++) {
        const line = view.state.doc.line(number);
        if (!active(view, line.from, line.to)) addLine(line.from, blockCode);
      }
    }
    if (node.name === 'CodeText' && node.node.parent?.name === 'CodeBlock' &&
        !active(view, node.from, node.to)) {
      add(view.state.doc.lineAt(node.from).from, node.from, hidden);
    }
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
    if (['HeaderMark', 'EmphasisMark', 'CodeMark', 'CodeInfo', 'QuoteMark'].includes(node.name)) {
      add(node.from, node.to, hidden);
      return;
    }
    if (node.name === 'Link') {
      const link = markdownLink(source, node.node);
      if (link) {
        add(node.from, node.from + 1, hidden);
        add(node.from + 1, link.labelEnd, Decoration.mark({ class: 'cm-preview-link', attributes: {
          'data-kind': link.kind, 'data-target': link.target
        } }));
        add(link.labelEnd, node.to, hidden);
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
