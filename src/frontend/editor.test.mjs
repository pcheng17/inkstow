import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { JSDOM } from 'jsdom';
import { EditorView } from '@codemirror/view';

const html = await readFile(new URL('./index.html', import.meta.url), 'utf8');
const folder = '/tmp/inkstow-throwaway';
const encoder = new TextEncoder();
const decoder = new TextDecoder('utf-8', { fatal: true });

async function setup(initial = { 'BOM.md': Buffer.from([0xef, 0xbb, 0xbf, ...encoder.encode('Hello')]), 'Other.md': Buffer.from('Second') }, startupError = '', startupDelay = 0) {
  const disk = new Map(Object.entries(initial));
  const calls = [];
  let notes = [];
  let hasVault = false;
  let path = '';
  let content = '';
  let saved = '';
  let bom = false;
  let injectedError = '';
  let pendingSave;
  let pendingEdit;
  let pendingSelect;
  let pendingMove;
  let pendingRefresh;
  let exits = 0;
  let change = 'none';
  let dismissedMissing = false;
  let expectedBytes;
  let reportedBytes;
  let trashAvailable = false;
  const opened = [];
  const external = [];
  function snapshot(ok, error) {
    return { ok, notes: [...notes], path, content, dirty: content !== saved, change, ...(error ? { error } : {}) };
  }
  const dom = new JSDOM(html, {
    url: 'https://inkstow.invalid/', runScripts: 'dangerously', pretendToBeVisual: true,
    beforeParse(window) {
      window.ResizeObserver = class { observe() {} unobserve() {} disconnect() {} };
      window.Range.prototype.getClientRects = () => [];
      window.Range.prototype.getBoundingClientRect = () => ({ top: 0, bottom: 0, left: 0, right: 0, width: 0, height: 0 });
      window.HTMLElement.prototype.getClientRects = () => [];
      window.HTMLElement.prototype.getBoundingClientRect = () => ({ top: 0, bottom: 0, left: 0, right: 0, width: 0, height: 0 });
      window.inkstowExit = () => { exits++; };
      window.inkstowRequest = async (request) => {
        calls.push(request);
        if (request.operation === 'save' && pendingSave) await pendingSave;
        if (request.operation === 'edit' && pendingEdit) await pendingEdit;
        if (request.operation === 'select' && pendingSelect) await pendingSelect;
        if (request.operation === 'move' && pendingMove) await pendingMove;
        if (request.operation === 'refresh' && pendingRefresh) await pendingRefresh;
        if (injectedError) {
          const message = injectedError; injectedError = '';
          if (message === 'Clean reload failed save') { content = saved = 'Outside'; change = 'reloaded'; }
          if (message === 'Could not serialize vault state.') return { ok: false, error: message };
          return snapshot(false, message);
        }
        switch (request.operation) {
          case 'startup':
            if (startupDelay) await new Promise((resolve) => setTimeout(resolve, startupDelay));
            return snapshot(!startupError, startupError);
          case 'pickVault':
            if (content !== saved) return snapshot(false, 'Unsaved changes.');
            hasVault = true; notes = [...disk.keys()].sort(); path = ''; content = saved = ''; return snapshot(true);
          case 'open':
            if (request.folder !== folder) return snapshot(false, 'Vault folder not found.');
            if (content !== saved) return snapshot(false, 'Unsaved changes.');
            hasVault = true; notes = [...disk.keys()].sort(); path = ''; content = saved = ''; return snapshot(true);
          case 'list': return snapshot(true);
          case 'follow':
            if (content !== saved) return snapshot(false, 'Save or discard your edits first.');
            if (request.link === 'image.png') return snapshot(true);
            if (!disk.has(`${request.link}.md`)) return snapshot(false, 'Link not found.');
            return window.inkstowRequest({ operation: 'select', path: `${request.link}.md` });
          case 'select': {
            if (content !== saved) return snapshot(false, 'Unsaved changes.');
            if (!disk.has(request.path)) return snapshot(false, 'Note not found.');
            const bytes = disk.get(request.path);
            bom = bytes.subarray(0, 3).equals(Buffer.from([0xef, 0xbb, 0xbf]));
            path = request.path;
            expectedBytes = Buffer.from(bytes);
            change = 'none'; dismissedMissing = false;
            content = saved = decoder.decode(bom ? bytes.subarray(3) : bytes);
            return snapshot(true);
          }
          case 'create': {
            if (content !== saved) return snapshot(false, 'Unsaved changes.');
            const name = request.path.endsWith('.md') ? request.path : `${request.path}.md`;
            if (disk.has(name)) return snapshot(false, 'Note already exists.');
            disk.set(name, Buffer.alloc(0)); notes = [...disk.keys()].sort();
            path = name; content = saved = ''; expectedBytes = Buffer.alloc(0); return snapshot(true);
          }
          case 'move': {
            if (content !== saved) return snapshot(false, 'Unsaved changes.');
            if (typeof request.updateLinks !== 'boolean') return snapshot(false, 'Invalid link choice.');
            const name = request.path.endsWith('.md') ? request.path : `${request.path}.md`;
            if (disk.has(name)) return snapshot(false, 'Note already exists.');
            disk.set(name, disk.get(path)); disk.delete(path); path = name;
            notes = [...disk.keys()].sort(); return snapshot(true);
          }
          case 'link': {
            if (content !== saved) return snapshot(false, 'Unsaved changes.');
            const target = request.link.split('#', 1)[0];
            const note = disk.has(target) ? target : `${target}.md`;
            if (disk.has(note) && note.endsWith('.md'))
              return window.inkstowRequest({ operation: 'select', path: note });
            if (!disk.has(target)) return snapshot(false, 'Link not found.');
            opened.push(target); return snapshot(true);
          }
          case 'follow':
            if (content !== saved) return snapshot(false, 'Unsaved changes.');
            if (!disk.has(request.link.endsWith('.md') ? request.link : `${request.link}.md`)) return snapshot(false, 'Link not found.');
            return window.inkstowRequest({ operation: 'select', path: request.link.endsWith('.md') ? request.link : `${request.link}.md` });
          case 'external':
            if (!/^https?:\/\/[^\s]+$/.test(request.url)) return snapshot(false, 'Invalid external URL.');
            external.push(request.url); return snapshot(true);
          case 'attachment':
            if (content !== saved) return snapshot(false, 'Unsaved changes.');
            if (!disk.has(request.link)) return snapshot(false, 'Attachment not found.');
            opened.push(request.link); return snapshot(true);
          case 'trash':
            if (content !== saved) return snapshot(false, 'Unsaved changes.');
            if (!trashAvailable) return snapshot(false, 'System trash unavailable.');
            disk.delete(path); notes = [...disk.keys()].sort(); path = ''; content = saved = ''; return snapshot(true);
          case 'edit':
            if (!path) return snapshot(false, 'No note selected.');
            content = request.content;
            if (content !== saved) dismissedMissing = false;
            return snapshot(true);
          case 'refresh': {
            if (!hasVault) return snapshot(true);
            notes = [...disk.keys()].sort();
            if (!path) return snapshot(true);
            const current = disk.get(path);
            if (!current) { change = dismissedMissing && content === saved ? 'none' : 'missing'; return snapshot(true, 'Note no longer exists.'); }
            dismissedMissing = false;
            if (!current.equals(expectedBytes)) {
              if (content !== saved) { change = 'conflict'; reportedBytes = Buffer.from(current); return snapshot(true, 'Note changed on disk.'); }
              expectedBytes = Buffer.from(current);
              content = saved = decoder.decode(current.subarray(bom ? 3 : 0));
              change = 'reloaded';
            } else change = 'none';
            return snapshot(true);
          }
          case 'reload': {
            if (!disk.has(path)) return snapshot(false, 'Note no longer exists.');
            const current = disk.get(path);
            expectedBytes = Buffer.from(current);
            content = saved = decoder.decode(current.subarray(bom ? 3 : 0));
            change = 'none'; return snapshot(true);
          }
          case 'merge':
            if (change !== 'conflict' || !disk.get(path)?.equals(reportedBytes)) return snapshot(false, 'Outside note changed again.');
            saved = decoder.decode(reportedBytes.subarray(bom ? 3 : 0));
            const separator = saved.match(/\r\n|\r|\n/)?.[0] || content.match(/\r\n|\r|\n/)?.[0] || '\n';
            content = `<<<<<<< mine\n${content}\n=======\n${saved}\n>>>>>>> outside`
              .replace(/\r\n|\r|\n/g, separator);
            expectedBytes = Buffer.from(reportedBytes); change = 'none'; return snapshot(true);
          case 'keepMine':
            if (change !== 'conflict' || !disk.get(path)?.equals(reportedBytes)) return snapshot(false, 'Outside note changed again.');
            expectedBytes = Buffer.from(reportedBytes);
            // fall through to save with the reported outside bytes.
          case 'save':
            if (!path) return snapshot(false, 'No note selected.');
            if (!disk.has(path)) { change = 'missing'; return snapshot(false, 'Note no longer exists.'); }
            if (!disk.get(path).equals(expectedBytes)) {
              if (content === saved) {
                expectedBytes = Buffer.from(disk.get(path));
                content = saved = decoder.decode(expectedBytes.subarray(bom ? 3 : 0));
                change = 'reloaded';
              } else { change = 'conflict'; reportedBytes = Buffer.from(disk.get(path)); }
              return snapshot(false, 'Note changed on disk.');
            }
            expectedBytes = Buffer.concat([bom ? Buffer.from([0xef, 0xbb, 0xbf]) : Buffer.alloc(0), Buffer.from(content)]);
            disk.set(path, Buffer.from(expectedBytes));
            saved = content; change = 'none'; return snapshot(true);
          case 'saveAsNew':
            if (change !== 'missing' || disk.has(path)) return snapshot(false, 'Note already exists.');
            expectedBytes = Buffer.concat([bom ? Buffer.from([0xef, 0xbb, 0xbf]) : Buffer.alloc(0), Buffer.from(content)]);
            disk.set(path, Buffer.from(expectedBytes)); saved = content; change = 'none'; return snapshot(true);
          case 'discard':
            if (change !== 'missing') return snapshot(false, 'No missing note.');
            content = saved; change = 'none'; dismissedMissing = true; return snapshot(true);
          default: return snapshot(false, 'Unknown operation.');
        }
      };
    }
  });
  const { document, Event } = dom.window;
  const pause = () => new Promise((resolve) => setTimeout(resolve, 35));
  async function open() {
    document.querySelector('#folder').value = folder;
    document.querySelector('#vault-form').dispatchEvent(new Event('submit', { bubbles: true, cancelable: true }));
    await pause();
  }
  async function select(name) {
    [...document.querySelectorAll('#notes button')].find((button) => button.textContent === name).click();
    await pause();
  }
  async function type(text) {
    const line = document.querySelector('.cm-line');
    line.textContent = text;
    document.querySelector('.cm-content').dispatchEvent(new Event('input', { bubbles: true }));
    await pause();
  }
  await pause(); // Let the initial last-vault lookup finish before driving controls.
  return { dom, document, disk, opened, external, calls, open, select, type, pause, exits: () => exits,
    enableTrash: () => { trashAvailable = true; }, injectError: (message) => { injectedError = message; },
    delaySave: () => {
      let release;
      pendingSave = new Promise((resolve) => { release = resolve; });
      return release;
    },
    delayEdit: () => {
      let release;
      pendingEdit = new Promise((resolve) => { release = resolve; });
      return release;
    },
    delayRefresh: () => {
      let release;
      pendingRefresh = new Promise((resolve) => { release = resolve; });
      return release;
    },
    delaySelect: () => {
      let release;
      pendingSelect = new Promise((resolve) => { release = resolve; });
      return release;
    },
    delayMove: () => {
      let release;
      pendingMove = new Promise((resolve) => { release = resolve; });
      return release;
    } };
}

test('inactive Markdown renders but selection across lines exposes source without changing saved bytes', async () => {
  const source = '# Heading\n**bold** and *italic* with `code`\n- list\n> quote\n[broken](x';
  const ui = await setup({ 'Note.md': Buffer.from(source) });
  try {
    await ui.open(); await ui.select('Note.md');
    const view = EditorView.findFromDOM(ui.document.querySelector('.cm-editor'));
    assert.ok(view);
    view.dispatch({ selection: { anchor: 0 } });
    assert.equal(ui.document.querySelectorAll('.cm-line')[0].textContent, '# Heading');
    assert.equal(ui.document.querySelectorAll('.cm-line')[1].textContent.includes('**'), false);
    assert.ok(ui.document.querySelector('.cm-strong'));
    assert.ok(ui.document.querySelector('.cm-emphasis'));
    assert.ok(ui.document.querySelector('.cm-inline-code'));
    assert.equal(ui.document.querySelectorAll('.cm-line')[2].textContent.includes('-'), false);
    assert.equal(ui.document.querySelectorAll('.cm-line')[2].textContent.includes('•'), true);
    assert.equal(ui.document.querySelectorAll('.cm-line')[3].textContent.includes('>'), false);
    assert.equal(ui.document.querySelectorAll('.cm-line')[4].textContent, '[broken](x');
    view.dispatch({ selection: { anchor: 11, head: view.state.doc.line(3).from + 1 } });
    assert.equal(ui.document.querySelectorAll('.cm-line')[1].textContent, '**bold** and *italic* with `code`');
    assert.equal(ui.document.querySelectorAll('.cm-line')[2].textContent, '- list');
    view.dispatch({ selection: { anchor: 0 } });
    ui.document.querySelector('#save').click(); await ui.pause();
    assert.deepEqual(ui.disk.get('Note.md'), Buffer.from(source));
  } finally { ui.dom.window.close(); }
});

test('fenced and indented code render inactive lines and restore editable source on the active line', async () => {
  const source = 'Intro\n```js\nconst x = 1;\n```\n\n    indented\n    next\nDone';
  const ui = await setup({ 'Note.md': Buffer.from(source) });
  try {
    await ui.open(); await ui.select('Note.md');
    const view = EditorView.findFromDOM(ui.document.querySelector('.cm-editor'));
    assert.equal(ui.document.querySelectorAll('.cm-line')[1].textContent, '');
    assert.equal(ui.document.querySelectorAll('.cm-line')[3].textContent, '');
    assert.ok(ui.document.querySelectorAll('.cm-line')[2].classList.contains('cm-block-code'));
    assert.equal(ui.document.querySelectorAll('.cm-line')[5].textContent, 'indented');
    assert.ok(ui.document.querySelectorAll('.cm-line')[5].classList.contains('cm-block-code'));
    view.dispatch({ selection: { anchor: view.state.doc.line(2).from + 1 } });
    assert.equal(ui.document.querySelectorAll('.cm-line')[1].textContent, '```js');
    view.dispatch({ selection: { anchor: view.state.doc.line(6).from + 2 } });
    assert.equal(ui.document.querySelectorAll('.cm-line')[5].textContent, '    indented');
    assert.equal(ui.document.querySelectorAll('.cm-line')[5].classList.contains('cm-block-code'), false);
    ui.document.querySelector('#save').click(); await ui.pause();
    assert.deepEqual(ui.disk.get('Note.md'), Buffer.from(source));
  } finally { ui.dom.window.close(); }
});

test('rendered links follow through the bridge while active source edits and shortcut follows', async () => {
  const ui = await setup({ 'Source.md': Buffer.from('Start\n[[Target|Alias]] [Site](https://example.org/a)\nEnd'),
    'Target.md': Buffer.from('Target') });
  try {
    await ui.open(); await ui.select('Source.md');
    const view = EditorView.findFromDOM(ui.document.querySelector('.cm-editor'));
    view.dispatch({ selection: { anchor: 0 } });
    const links = ui.document.querySelectorAll('.cm-preview-link');
    assert.deepEqual([...links].map((link) => link.textContent), ['Alias', 'Site']);
    links[1].dispatchEvent(new ui.dom.window.MouseEvent('click', { bubbles: true }));
    await ui.pause();
    assert.deepEqual(ui.external, ['https://example.org/a']);
    view.dispatch({ selection: { anchor: view.state.doc.line(2).from + 4 } });
    assert.match(ui.document.querySelectorAll('.cm-line')[1].textContent, /\[\[Target\|Alias\]\]/);
    const sourceLine = ui.document.querySelectorAll('.cm-line')[1];
    sourceLine.dispatchEvent(new ui.dom.window.MouseEvent('click', { bubbles: true }));
    await ui.pause();
    assert.equal(view.state.selection.main.head, view.state.doc.line(2).from + 4);
    assert.equal(ui.document.querySelector('#title').textContent, 'Source.md');
    sourceLine.textContent = '[[Target|Edited]] [Site](https://example.org/a)';
    ui.document.querySelector('.cm-content').dispatchEvent(new ui.dom.window.Event('input', { bubbles: true }));
    await ui.pause();
    assert.equal(ui.calls.at(-1).operation, 'edit');
    assert.match(ui.calls.at(-1).content, /\[\[Target\|Edited\]\]/);
    ui.document.querySelector('#undo').click(); await ui.pause();
    ui.document.querySelector('.cm-content').dispatchEvent(new ui.dom.window.KeyboardEvent('keydown',
      { key: 'Enter', altKey: true, bubbles: true }));
    await ui.pause();
    assert.equal(ui.document.querySelector('#title').textContent, 'Target.md');
  } finally { ui.dom.window.close(); }
});

test('relative Markdown note links render and use core link resolution, not webview navigation', async () => {
  const ui = await setup({ 'Source.md': Buffer.from('Intro\n[Target](Target.md) [Picture](image.png)'),
    'Target.md': Buffer.from('Target'), 'image.png': Buffer.from('image') });
  try {
    await ui.open(); await ui.select('Source.md');
    assert.deepEqual([...ui.document.querySelectorAll('.cm-preview-link')].map((node) => node.textContent),
      ['Target', 'Picture']);
    ui.document.querySelectorAll('.cm-preview-link')[1].click(); await ui.pause();
    assert.deepEqual(ui.opened, ['image.png']);
    ui.document.querySelector('.cm-preview-link').click(); await ui.pause();
    assert.equal(ui.document.querySelector('#title').textContent, 'Target.md');
    assert.equal(ui.calls.some((call) => call.operation === 'external'), false);
  } finally { ui.dom.window.close(); }
});

test('Markdown angle destinations and titled links render and follow through the bridge', async () => {
  const source = 'Intro\n[Second](<Second note.md>) [Target](Target.md "title") [Site](https://example.org/a "site")';
  const ui = await setup({ 'Source.md': Buffer.from(source),
    'Second note.md': Buffer.from('Second'), 'Target.md': Buffer.from('Target') });
  try {
    await ui.open(); await ui.select('Source.md');
    const links = () => [...ui.document.querySelectorAll('.cm-preview-link')];
    assert.deepEqual(links().map((link) => link.textContent), ['Second', 'Target', 'Site']);
    assert.deepEqual(links().map((link) => link.dataset.target),
      ['Second note.md', 'Target.md', 'https://example.org/a']);
    links()[2].click(); await ui.pause();
    assert.deepEqual(ui.external, ['https://example.org/a']);
    links()[0].click(); await ui.pause();
    assert.equal(ui.document.querySelector('#title').textContent, 'Second note.md');
    await ui.select('Source.md');
    links()[1].click(); await ui.pause();
    assert.equal(ui.document.querySelector('#title').textContent, 'Target.md');
  } finally { ui.dom.window.close(); }
});

test('source shortcut follows parsed Markdown destinations with spaces and titles', async () => {
  const source = 'Intro\n[Second](<Second note.md>) [Target](Target.md "title") [Site](https://example.org/a "site")';
  const ui = await setup({ 'Source.md': Buffer.from(source),
    'Second note.md': Buffer.from('Second'), 'Target.md': Buffer.from('Target') });
  try {
    await ui.open(); await ui.select('Source.md');
    const shortcut = async (label) => {
      const view = EditorView.findFromDOM(ui.document.querySelector('.cm-editor'));
      const line = view.state.doc.line(2);
      view.dispatch({ selection: { anchor: line.from + line.text.indexOf(label) + 2 } });
      assert.match(ui.document.querySelectorAll('.cm-line')[1].textContent, /\[Second\]\(<Second note\.md>\)/);
      ui.document.querySelector('.cm-content').dispatchEvent(new ui.dom.window.KeyboardEvent('keydown',
        { key: 'Enter', altKey: true, bubbles: true }));
      await ui.pause();
    };
    await shortcut('[Second]');
    assert.equal(ui.document.querySelector('#title').textContent, 'Second note.md');
    await ui.select('Source.md');
    await shortcut('[Target]');
    assert.equal(ui.document.querySelector('#title').textContent, 'Target.md');
    await ui.select('Source.md');
    await shortcut('[Site]');
    assert.deepEqual(ui.external, ['https://example.org/a']);
    assert.deepEqual(ui.calls.filter((call) => call.operation === 'link').map((call) => call.link),
      ['Second note.md', 'Target.md']);
  } finally { ui.dom.window.close(); }
});

test('escaped Markdown punctuation resolves the same note from preview and source', async () => {
  const source = 'Intro\n[Target](Target\\(1\\).md)';
  const ui = await setup({ 'Source.md': Buffer.from(source), 'Target(1).md': Buffer.from('Target') });
  try {
    await ui.open(); await ui.select('Source.md');
    const rendered = ui.document.querySelector('.cm-preview-link');
    assert.equal(rendered?.dataset.target, 'Target(1).md');
    rendered.click(); await ui.pause();
    assert.equal(ui.document.querySelector('#title').textContent, 'Target(1).md');
    await ui.select('Source.md');
    const view = EditorView.findFromDOM(ui.document.querySelector('.cm-editor'));
    view.dispatch({ selection: { anchor: view.state.doc.line(2).from + 2 } });
    ui.document.querySelector('.cm-content').dispatchEvent(new ui.dom.window.KeyboardEvent('keydown',
      { key: 'Enter', altKey: true, bubbles: true }));
    await ui.pause();
    assert.equal(ui.document.querySelector('#title').textContent, 'Target(1).md');
  } finally { ui.dom.window.close(); }
});

test('wikilink dots and Markdown fragments use core target type, not an extension guess', async () => {
  const ui = await setup({ 'Source.md': Buffer.from('Intro\n[[Foo.bar]] [Target](Target.md#Section)'),
    'Foo.bar.md': Buffer.from('Note with a dot'), 'Target.md': Buffer.from('Target') });
  try {
    await ui.open(); await ui.select('Source.md');
    ui.document.querySelector('.cm-preview-link').click(); await ui.pause();
    assert.equal(ui.document.querySelector('#title').textContent, 'Foo.bar.md');
    await ui.select('Source.md');
    ui.document.querySelectorAll('.cm-preview-link')[1].click(); await ui.pause();
    assert.equal(ui.document.querySelector('#title').textContent, 'Target.md');
    assert.deepEqual(ui.opened, []);
    assert.deepEqual(ui.calls.filter((call) => call.operation === 'link').map((call) => call.link),
      ['Foo.bar', 'Target.md#Section']);
  } finally { ui.dom.window.close(); }
});

test('escaped link delimiters remain literal and cannot follow by shortcut', async () => {
  const source = 'Intro\n\\[[Target]] \\[Target](Target.md) \\[Site](https://example.org) \\\\[[Target]]\nEnd';
  const ui = await setup({ 'Source.md': Buffer.from(source), 'Target.md': Buffer.from('Target') });
  try {
    await ui.open(); await ui.select('Source.md');
    const view = EditorView.findFromDOM(ui.document.querySelector('.cm-editor'));
    assert.deepEqual([...ui.document.querySelectorAll('.cm-preview-link')].map((node) => node.textContent), ['Target']);
    assert.match(ui.document.querySelectorAll('.cm-line')[1].textContent, /\\\[\[Target\]\] \\\[Target\]\(Target\.md\)/);
    for (const marker of ['\\[[Target]]', '\\[Target](Target.md)', '\\[Site](https://example.org)']) {
      const pos = view.state.doc.line(2).from + view.state.doc.line(2).text.indexOf(marker) + marker.length - 2;
      view.dispatch({ selection: { anchor: pos } });
      ui.document.querySelector('.cm-content').dispatchEvent(new ui.dom.window.KeyboardEvent('keydown',
        { key: 'Enter', altKey: true, bubbles: true }));
      await ui.pause();
      assert.equal(ui.document.querySelector('#title').textContent, 'Source.md');
      assert.equal(ui.calls.filter((call) => ['link', 'external'].includes(call.operation)).length, 0);
    }
  } finally { ui.dom.window.close(); }
});

test('dirty rendered navigation is refused and undo preserves source and CRLF bytes', async () => {
  const source = '# Title\r\n[[Target]]\r\nNBSP:\u00a0 U+2028:\u2028 U+2029:\u2029';
  const ui = await setup({ 'Source.md': Buffer.from(source), 'Target.md': Buffer.from('Target') });
  try {
    await ui.open(); await ui.select('Source.md');
    const view = EditorView.findFromDOM(ui.document.querySelector('.cm-editor'));
    view.dispatch({ selection: { anchor: 0 } });
    assert.equal(ui.document.querySelector('.cm-preview-link').textContent, 'Target');
    await ui.type('# Draft');
    ui.document.querySelector('.cm-preview-link').click(); await ui.pause();
    assert.match(ui.document.querySelector('#error').textContent, /Unsaved changes/i);
    assert.equal(ui.document.querySelector('#title').textContent, 'Source.md');
    ui.document.querySelector('#undo').click(); await ui.pause();
    ui.document.querySelector('#save').click(); await ui.pause();
    assert.deepEqual(ui.disk.get('Source.md'), Buffer.from(source));
  } finally { ui.dom.window.close(); }
});

test('empty wikilink labels stay visible and editable on inactive lines', async () => {
  const ui = await setup({ 'Source.md': Buffer.from('Start\n[[Target|]] and [[|Alias]]\nEnd') });
  try {
    await ui.open(); await ui.select('Source.md');
    assert.equal(ui.document.querySelectorAll('.cm-line')[1].textContent, '[[Target|]] and [[|Alias]]');
    assert.equal(ui.document.querySelector('.cm-preview-link'), null);
    const view = EditorView.findFromDOM(ui.document.querySelector('.cm-editor'));
    view.dispatch({ selection: { anchor: view.state.doc.line(2).from + 4 },
      changes: { from: view.state.doc.line(2).to, insert: ' more' } });
    await ui.pause();
    assert.equal(ui.calls.at(-1).content, 'Start\n[[Target|]] and [[|Alias]] more\nEnd');
  } finally { ui.dom.window.close(); }
});

test('malformed links and code literals remain source', async () => {
  const ui = await setup({ 'Source.md': Buffer.from('Start\n`[[Hidden]]` [[broken] [x](bad url)\nEnd') });
  try {
    await ui.open(); await ui.select('Source.md');
    assert.equal(ui.document.querySelector('.cm-preview-link'), null);
    const view = EditorView.findFromDOM(ui.document.querySelector('.cm-editor'));
    view.dispatch({ selection: { anchor: view.state.doc.line(2).from + 4 } });
    ui.document.querySelector('.cm-content').dispatchEvent(new ui.dom.window.KeyboardEvent('keydown',
      { key: 'Enter', altKey: true, bubbles: true }));
    await ui.pause();
    assert.equal(ui.calls.filter((call) => ['follow', 'link'].includes(call.operation)).length, 0);
    assert.equal(ui.document.querySelector('#title').textContent, 'Source.md');
  } finally { ui.dom.window.close(); }
});

test('composition input edits active link source without following its shortcut', async () => {
  const ui = await setup({ 'Source.md': Buffer.from('Intro\n[[Target]]\nEnd'), 'Target.md': Buffer.from('Target') });
  try {
    await ui.open(); await ui.select('Source.md');
    const view = EditorView.findFromDOM(ui.document.querySelector('.cm-editor'));
    view.dispatch({ selection: { anchor: view.state.doc.line(2).from + 4 } });
    const editor = ui.document.querySelector('.cm-content');
    editor.focus();
    editor.dispatchEvent(new ui.dom.window.CompositionEvent('compositionstart', { bubbles: true, data: '語' }));
    editor.dispatchEvent(new ui.dom.window.KeyboardEvent('keydown',
      { key: 'Enter', altKey: true, bubbles: true }));
    ui.dom.window.inkstowFollow();
    assert.equal(ui.document.querySelector('#error').hidden, true);
    assert.equal(ui.document.querySelector('#title').textContent, 'Source.md');
    editor.dispatchEvent(new ui.dom.window.InputEvent('beforeinput',
      { bubbles: true, inputType: 'insertCompositionText', data: '語', isComposing: true }));
    ui.document.querySelectorAll('.cm-line')[1].textContent = '[[Target語]]';
    editor.dispatchEvent(new ui.dom.window.InputEvent('input',
      { bubbles: true, inputType: 'insertCompositionText', data: '語', isComposing: true }));
    editor.dispatchEvent(new ui.dom.window.CompositionEvent('compositionend', { bubbles: true, data: '語' }));
    await ui.pause();
    assert.equal(ui.document.querySelector('#title').textContent, 'Source.md');
    assert.equal(ui.calls.filter((call) => call.operation === 'link').length, 0);
    assert.equal(ui.calls.at(-1).content, 'Intro\n[[Target語]]\nEnd');
  } finally { ui.dom.window.close(); }
});

test('bundled editor has no remote runtime assets or links', () => {
  assert.match(html, /cm-editor/); // CodeMirror is bundled into the script, not imported at runtime.
  assert.doesNotMatch(html, /<script[^>]+src=|<link[^>]+href=|<a\s/i);
  assert.match(html, /connect-src 'none'/);
  const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
  assert.ok(html.includes(`script-src 'sha256-${createHash('sha256').update(script).digest('base64')}'`));
  assert.doesNotMatch(html, /script-src 'unsafe-inline'/);
});

test('native picker opens vault and startup failure remains visible', async () => {
  const ui = await setup();
  try {
    await ui.pause();
    ui.document.querySelector('#pick-vault').click();
    await ui.pause();
    assert.equal(ui.calls.at(-1).operation, 'pickVault');
    assert.equal(ui.document.querySelectorAll('#notes button').length, 2);
    // Startup is requested when the editor first loads, not on a button click.
    assert.ok(ui.calls.some((call) => call.operation === 'startup'));
  } finally { ui.dom.window.close(); }
});

test('missing last vault stays explained across refresh until a vault opens', async () => {
  const ui = await setup(undefined, 'Could not reopen last vault: folder missing.');
  try {
    const message = ui.document.querySelector('#error');
    assert.match(message.textContent, /folder missing/);
    ui.dom.window.dispatchEvent(new ui.dom.window.Event('focus'));
    await ui.pause();
    assert.equal(message.hidden, false);
    assert.match(message.textContent, /folder missing/);
    await ui.open();
    assert.equal(message.hidden, true);
  } finally { ui.dom.window.close(); }
});

test('follow shortcut requests the cursor link and refuses dirty attachment navigation', async () => {
  const ui = await setup({ 'Note.md': Buffer.from('[[image.png]]') });
  try {
    await ui.open(); await ui.select('Note.md');
    ui.dom.window.inkstowFollow(); await ui.pause();
    assert.equal(ui.calls.at(-1).link, 'image.png');
    await ui.type('[[image.png]] more');
    ui.dom.window.inkstowFollow(); await ui.pause();
    assert.equal(ui.document.querySelector('#dirty').hidden, false);
    assert.match(ui.document.querySelector('#error').textContent, /Unsaved changes/);
  } finally { ui.dom.window.close(); }
});

test('failed save on quit never exits even when outside clean reload clears dirty', async () => {
  const ui = await setup();
  try {
    await ui.open(); await ui.select('BOM.md'); await ui.type('Mine');
    ui.disk.set('BOM.md', Buffer.from([0xef, 0xbb, 0xbf, ...encoder.encode('Outside')]));
    ui.dom.window.inkstowAskQuit(); await ui.pause();
    // The bridge can reject a save and report a clean reload after a concurrent edit reversal.
    ui.injectError('Clean reload failed save');
    ui.document.querySelector('#quit-save').click(); await ui.pause();
    assert.equal(ui.exits(), 0);
    assert.equal(ui.document.querySelector('#quit-prompt').hidden, false);
  } finally { ui.dom.window.close(); }
});

test('Cancel during a pending Save and quit ignores queued quit requests after saving', async () => {
  const ui = await setup();
  try {
    await ui.open(); await ui.select('BOM.md'); await ui.type('Mine');
    const releaseSave = ui.delaySave();
    ui.dom.window.inkstowAskQuit(); await ui.pause();
    ui.document.querySelector('#quit-save').click(); await ui.pause();
    assert.equal(ui.calls.at(-1).operation, 'save');
    assert.equal(ui.document.querySelector('#save').disabled, true);
    assert.equal(ui.document.querySelector('#notes button').disabled, true);
    ui.dom.window.inkstowAskQuit(); // A second Quit waits behind the pending save.
    ui.document.querySelector('#quit-cancel').click();
    assert.equal(ui.document.querySelector('#quit-prompt').hidden, true);
    releaseSave(); await ui.pause();
    assert.equal(ui.exits(), 0);
    assert.equal(ui.document.querySelector('#quit-prompt').hidden, true);
    assert.equal(ui.document.querySelector('#dirty').hidden, true);
    assert.equal(ui.disk.get('BOM.md').subarray(3).toString(), 'Mine');
  } finally { ui.dom.window.close(); }
});

test('quit offers cancel, save and discard; conflict and failed save keep edits', async () => {
  const ui = await setup();
  try {
    await ui.open(); await ui.select('BOM.md'); await ui.type('Mine');
    ui.dom.window.inkstowAskQuit(); await ui.pause();
    assert.equal(ui.document.querySelector('#quit-prompt').hidden, false);
    assert.equal(ui.document.activeElement.id, 'quit-cancel');
    ui.document.querySelector('#quit-cancel').click(); await ui.pause();
    assert.equal(ui.exits(), 0);
    ui.disk.set('BOM.md', Buffer.from([0xef, 0xbb, 0xbf, ...encoder.encode('Outside')]));
    ui.dom.window.inkstowAskQuit(); await ui.pause();
    ui.document.querySelector('#quit-save').click(); await ui.pause();
    assert.equal(ui.exits(), 0);
    assert.equal(ui.document.querySelector('#dirty').hidden, false);
    assert.match(ui.document.querySelector('#error').textContent, /changed on disk/);
    assert.equal(ui.document.querySelector('#quit-prompt').hidden, false);
    ui.document.querySelector('#quit-discard').click(); await ui.pause();
    assert.equal(ui.exits(), 1);
    assert.equal(ui.disk.get('BOM.md').subarray(3).toString(), 'Outside');
  } finally { ui.dom.window.close(); }
});

test('unchanged refresh leaves the empty window and note view untouched', async () => {
  const ui = await setup();
  try {
    for (const selected of [false, true]) {
      if (selected) { await ui.open(); await ui.select('BOM.md'); }
      const button = ui.document.querySelector('#notes button');
      const editor = ui.document.querySelector('.cm-editor');
      const mutations = [];
      const observer = new ui.dom.window.MutationObserver((changes) => mutations.push(...changes));
      observer.observe(ui.document.body, { attributes: true, childList: true, subtree: true, characterData: true });
      await ui.dom.window.inkstowRefresh();
      await ui.pause();
      observer.disconnect();
      assert.equal(ui.calls.at(-1).operation, 'refresh');
      // The editor briefly toggles contenteditable while the bridge request is in flight.
      assert.deepEqual(mutations.filter((change) => !change.target.closest?.('.cm-editor'))
        .map((change) => `${change.target.id || change.target.tagName}:${change.type}`), []);
      assert.equal(ui.document.querySelector('#notes button'), button);
      assert.equal(ui.document.querySelector('.cm-editor'), editor);
    }
  } finally { ui.dom.window.close(); }
});

test('background refresh blocks note actions without visually disabling the window', async () => {
  const ui = await setup();
  try {
    await ui.open(); await ui.select('BOM.md');
    const release = ui.delayRefresh();
    const refreshing = ui.dom.window.inkstowRefresh();
    await ui.pause();
    assert.equal(ui.calls.at(-1).operation, 'refresh');
    await new Promise((resolve) => setTimeout(resolve, 170));
    assert.equal(ui.document.querySelector('#busy').textContent, '');
    assert.equal(ui.document.querySelector('#move').disabled, false);
    ui.document.querySelector('#move').click();
    ui.document.querySelector('#trash').click();
    assert.equal(ui.document.querySelector('#move-choice').hidden, true);
    assert.equal(ui.document.querySelector('#trash-choice').hidden, true);
    release(); await refreshing;
    ui.document.querySelector('#move').click();
    assert.equal(ui.document.querySelector('#move-choice').hidden, false);
    const releaseAgain = ui.delayRefresh();
    const refreshingAgain = ui.dom.window.inkstowRefresh();
    await ui.pause();
    ui.document.querySelector('#move-with-links').click();
    assert.equal(ui.document.querySelector('#move-choice').hidden, false);
    assert.equal(ui.calls.filter((call) => call.operation === 'move').length, 0);
    releaseAgain(); await refreshingAgain;
  } finally { ui.dom.window.close(); }
});

test('focus refresh discovers outside folder changes without a selected note', async () => {
  const ui = await setup();
  try {
    await ui.open();
    const existing = ui.document.querySelector('#notes button');
    ui.disk.set('Outside.md', Buffer.from('Outside'));
    ui.dom.window.dispatchEvent(new ui.dom.window.Event('focus'));
    await ui.pause();
    assert.deepEqual([...ui.document.querySelectorAll('#notes button')].map((b) => b.textContent),
      ['BOM.md', 'Other.md', 'Outside.md']);
    assert.equal(ui.document.querySelector('#notes button'), existing);
  } finally { ui.dom.window.close(); }
});

test('slow startup does not display a user-action busy status', async () => {
  const ui = await setup(undefined, '', 250);
  try {
    await new Promise((resolve) => setTimeout(resolve, 170));
    assert.equal(ui.document.querySelector('#busy').textContent, '');
    await new Promise((resolve) => setTimeout(resolve, 90));
    assert.equal(ui.calls.at(-1).operation, 'startup');
  } finally { ui.dom.window.close(); }
});

test('selecting another note does not dim the entire window while loading', async () => {
  const ui = await setup();
  try {
    await ui.open(); await ui.select('BOM.md');
    const release = ui.delaySelect();
    const current = ui.document.querySelector('#notes button[aria-current="true"]');
    const next = [...ui.document.querySelectorAll('#notes button')].find((b) => b.textContent === 'Other.md');
    next.click(); await ui.pause();
    assert.equal(ui.calls.at(-1).operation, 'select');
    assert.equal(ui.document.querySelector('#folder').disabled, false);
    assert.equal(ui.document.querySelector('#save').disabled, false);
    assert.equal(current.disabled, false);
    assert.equal(next.disabled, false);
    assert.equal(ui.document.querySelector('.cm-line').textContent, 'Hello');
    await new Promise((resolve) => setTimeout(resolve, 170));
    assert.equal(ui.document.querySelector('#busy').textContent, 'Working…');
    next.click(); current.click();
    assert.equal(ui.calls.filter((call) => call.operation === 'select').length, 2);
    release(); await ui.pause();
    assert.equal(ui.document.querySelector('#title').textContent, 'Other.md');
    assert.equal(ui.document.querySelector('.cm-line').textContent, 'Second');
    assert.equal(ui.document.querySelector('#busy').textContent, '');
  } finally { ui.dom.window.close(); }
});

test('an edit reply during a pending selection cannot dim the window', async () => {
  const ui = await setup();
  try {
    await ui.open(); await ui.select('BOM.md');
    const releaseEdit = ui.delayEdit();
    const releaseSelect = ui.delaySelect();
    await ui.type('Changed');
    const next = [...ui.document.querySelectorAll('#notes button')].find((b) => b.textContent === 'Other.md');
    next.click(); await ui.pause();
    releaseEdit(); await ui.pause();
    assert.equal(ui.calls.at(-1).operation, 'select');
    assert.equal(ui.document.querySelector('#folder').disabled, false);
    assert.equal(ui.document.querySelector('#save').disabled, false);
    assert.equal(next.disabled, false);
    releaseSelect(); await ui.pause();
    assert.equal(ui.document.querySelector('#title').textContent, 'BOM.md');
    assert.equal(ui.document.querySelector('.cm-line').textContent, 'Changed');
  } finally { ui.dom.window.close(); }
});

test('saving does not dim unrelated controls while the bridge is busy', async () => {
  const ui = await setup();
  try {
    await ui.open(); await ui.select('BOM.md'); await ui.type('Changed');
    const release = ui.delaySave();
    ui.document.querySelector('#save').click(); await ui.pause();
    assert.equal(ui.calls.at(-1).operation, 'save');
    assert.equal(ui.document.querySelector('#folder').disabled, false);
    assert.equal(ui.document.querySelector('#notes button').disabled, false);
    release(); await ui.pause();
    assert.equal(ui.document.querySelector('#dirty').hidden, true);
  } finally { ui.dom.window.close(); }
});

test('move and trash decisions dismiss on Escape without sending a bridge mutation', async () => {
  const ui = await setup();
  try {
    await ui.open(); await ui.select('BOM.md');
    for (const [button, panel] of [['move', 'move-choice'], ['trash', 'trash-choice']]) {
      ui.document.querySelector(`#${button}`).click();
      assert.equal(ui.document.querySelector(`#${panel}`).hidden, false);
      ui.document.querySelector(`#${panel}`).dispatchEvent(new ui.dom.window.KeyboardEvent('keydown',
        { key: 'Escape', bubbles: true, cancelable: true }));
      assert.equal(ui.document.querySelector(`#${panel}`).hidden, true);
    }
    assert.equal(ui.calls.filter((call) => ['move', 'trash'].includes(call.operation)).length, 0);
    assert.equal(ui.document.querySelector('#title').textContent, 'BOM.md');
  } finally { ui.dom.window.close(); }
});

test('moving a note does not dim the window while the bridge is busy', async () => {
  const ui = await setup();
  try {
    await ui.open(); await ui.select('BOM.md');
    const release = ui.delayMove();
    ui.document.querySelector('#move').click();
    ui.document.querySelector('#move-path').value = 'Renamed';
    ui.document.querySelector('#move-with-links').click(); await ui.pause();
    assert.equal(ui.calls.at(-1).operation, 'move');
    assert.equal(ui.document.querySelector('#folder').disabled, false);
    assert.equal(ui.document.querySelector('#notes button').disabled, false);
    release(); await ui.pause();
    assert.equal(ui.document.querySelector('#title').textContent, 'Renamed.md');
  } finally { ui.dom.window.close(); }
});

test('refresh adds a note without replacing the selected note or editor', async () => {
  const ui = await setup();
  try {
    await ui.open(); await ui.select('BOM.md');
    const existing = ui.document.querySelector('#notes button');
    const editor = ui.document.querySelector('.cm-editor');
    ui.disk.set('Outside.md', Buffer.from('New note'));
    await ui.dom.window.inkstowRefresh();
    assert.deepEqual([...ui.document.querySelectorAll('#notes button')].map((b) => b.textContent),
      ['BOM.md', 'Other.md', 'Outside.md']);
    assert.equal(ui.document.querySelector('#notes button'), existing);
    assert.equal(ui.document.querySelector('.cm-editor'), editor);
    assert.equal(ui.document.querySelector('.cm-line').textContent, 'Hello');
  } finally { ui.dom.window.close(); }
});

test('refresh removes only a deleted note from the list', async () => {
  const ui = await setup();
  try {
    await ui.open();
    const remaining = ui.document.querySelector('#notes button');
    ui.disk.delete('Other.md');
    await ui.dom.window.inkstowRefresh();
    assert.deepEqual([...ui.document.querySelectorAll('#notes button')].map((b) => b.textContent), ['BOM.md']);
    assert.equal(ui.document.querySelector('#notes button'), remaining);
  } finally { ui.dom.window.close(); }
});

test('refresh removes an early note without moving surviving buttons', async () => {
  const ui = await setup();
  try {
    await ui.open();
    const survivor = ui.document.querySelectorAll('#notes button')[1];
    const moved = [];
    const observer = new ui.dom.window.MutationObserver((changes) => {
      for (const change of changes) moved.push(...change.addedNodes);
    });
    observer.observe(ui.document.querySelector('#notes'), { childList: true });
    ui.disk.delete('BOM.md');
    await ui.dom.window.inkstowRefresh();
    observer.disconnect();
    assert.deepEqual([...ui.document.querySelectorAll('#notes button')].map((b) => b.textContent), ['Other.md']);
    assert.equal(ui.document.querySelector('#notes button'), survivor);
    assert.deepEqual(moved, []);
  } finally { ui.dom.window.close(); }
});

test('open, edit, selection, undo and BOM-preserving save', async () => {
  const ui = await setup();
  try {
    await ui.open();
    assert.deepEqual([...ui.document.querySelectorAll('#notes button')].map((b) => b.textContent), ['BOM.md', 'Other.md']);
    await ui.select('BOM.md');
    assert.equal(ui.document.querySelector('.cm-line').textContent, 'Hello');
    const range = ui.document.createRange();
    range.setStart(ui.document.querySelector('.cm-line').firstChild, 0);
    range.setEnd(ui.document.querySelector('.cm-line').firstChild, 5);
    ui.dom.window.getSelection().addRange(range);
    assert.equal(ui.dom.window.getSelection().toString(), 'Hello');
    await ui.type('Hello edited');
    assert.equal(ui.calls.at(-1).operation, 'edit');
    assert.equal(ui.document.querySelector('#dirty').hidden, false);
    assert.equal(ui.document.querySelector('#title').textContent, 'BOM.md');
    ui.document.querySelector('#undo').click();
    await ui.pause();
    assert.equal(ui.document.querySelector('.cm-line').textContent, 'Hello');
    await ui.type('Hello saved');
    ui.document.querySelector('#save').click();
    await ui.pause();
    assert.deepEqual(ui.disk.get('BOM.md'), Buffer.from([0xef, 0xbb, 0xbf, ...encoder.encode('Hello saved')]));
    assert.equal(ui.document.querySelector('#dirty').hidden, true);
    await ui.select('Other.md');
    assert.equal(ui.document.querySelector('#title').textContent, 'Other.md');
  } finally { ui.dom.window.close(); }
});

test('uniform CRLF, LF and CR notes preserve BOM, separators and special UTF-8 through edit and save', async () => {
  for (const separator of ['\r\n', '\n', '\r']) {
    for (const hasBom of [false, true]) {
      const original = `Start${separator}NBSP:\u00a0 U+2028:\u2028 U+2029:\u2029${separator}End${separator}`;
      const prefix = hasBom ? Buffer.from([0xef, 0xbb, 0xbf]) : Buffer.alloc(0);
      const bytes = (text) => Buffer.concat([prefix, Buffer.from(text)]);
      const ui = await setup({ 'Separators.md': bytes(original) });
      try {
        await ui.open(); await ui.select('Separators.md');
        assert.equal(ui.document.querySelector('#dirty').hidden, true, `pristine ${JSON.stringify(separator)}`);
        assert.equal(ui.document.querySelector('#error').hidden, true);
        ui.document.querySelector('#save').click(); await ui.pause();
        assert.deepEqual(ui.disk.get('Separators.md'), bytes(original), 'save without edits');
        await ui.type('Edited');
        assert.equal(ui.calls.at(-1).content, `Edited${separator}NBSP:\u00a0 U+2028:\u2028 U+2029:\u2029${separator}End${separator}`);
        assert.equal(ui.document.querySelector('#dirty').hidden, false);
        ui.document.querySelector('#save').click(); await ui.pause();
        assert.deepEqual(ui.disk.get('Separators.md'), bytes(`Edited${separator}NBSP:\u00a0 U+2028:\u2028 U+2029:\u2029${separator}End${separator}`));
        assert.equal(ui.document.querySelector('#dirty').hidden, true);
      } finally { ui.dom.window.close(); }
    }
  }
});

test('mixed CRLF, LF and CR notes are read-only and cannot save, retaining disk bytes', async () => {
  const original = 'Start\r\nMiddle\nNBSP:\u00a0 U+2028:\u2028 U+2029:\u2029\rEnd';
  const bytes = Buffer.from([0xef, 0xbb, 0xbf, ...encoder.encode(original)]);
  const ui = await setup({ 'Mixed.md': bytes, 'Other.md': Buffer.from('Other') });
  try {
    await ui.open(); await ui.select('Mixed.md');
    assert.match(ui.document.querySelector('#error').textContent, /mixed.*line endings/i);
    assert.equal(ui.document.querySelector('.cm-content').getAttribute('contenteditable'), 'false');
    assert.equal(ui.document.querySelector('#dirty').hidden, true);
    assert.equal(ui.document.querySelector('#save').disabled, true);
    assert.equal(ui.document.querySelector('#undo').disabled, true);
    ui.document.querySelector('.cm-content').dispatchEvent(new ui.dom.window.KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
    ui.document.dispatchEvent(new ui.dom.window.KeyboardEvent('keydown', { key: 's', ctrlKey: true, bubbles: true }));
    await ui.pause();
    assert.equal(ui.calls.filter((call) => ['edit', 'save'].includes(call.operation)).length, 0);
    assert.deepEqual(ui.disk.get('Mixed.md'), bytes);
    await ui.select('Other.md');
    assert.equal(ui.document.querySelector('#error').hidden, true);
    assert.equal(ui.document.querySelector('#save').disabled, false);
  } finally { ui.dom.window.close(); }
});

test('dirty note refuses navigation, save failures and bridge errors keep text visible', async () => {
  const ui = await setup();
  try {
    await ui.open(); await ui.select('BOM.md');
    await ui.type('unsaved');
    await ui.select('Other.md');
    assert.match(ui.document.querySelector('#error').textContent, /Unsaved changes/);
    assert.equal(ui.document.querySelector('#notes button[aria-current="true"]').textContent, 'BOM.md');
    assert.equal(ui.document.querySelector('.cm-line').textContent, 'unsaved');
    ui.injectError('Disk changed outside Inkstow.');
    ui.document.querySelector('#save').click(); await ui.pause();
    assert.match(ui.document.querySelector('#error').textContent, /Disk changed outside Inkstow/);
    assert.equal(ui.document.querySelector('#dirty').hidden, false);
    assert.equal(ui.document.querySelector('.cm-line').textContent, 'unsaved');
    assert.deepEqual(ui.disk.get('BOM.md'), Buffer.from([0xef, 0xbb, 0xbf, ...encoder.encode('Hello')]));
    ui.document.querySelector('#vault-form').dispatchEvent(new ui.dom.window.Event('submit', { bubbles: true, cancelable: true }));
    await ui.pause();
    assert.match(ui.document.querySelector('#error').textContent, /Unsaved changes/);
    assert.equal(ui.document.querySelector('.cm-line').textContent, 'unsaved');
    ui.document.querySelector('#folder').value = '/not-a-vault';
    ui.document.querySelector('#vault-form').dispatchEvent(new ui.dom.window.Event('submit', { bubbles: true, cancelable: true }));
    await ui.pause();
    assert.match(ui.document.querySelector('#error').textContent, /Vault folder not found/);
    assert.equal(ui.document.querySelector('.cm-line').textContent, 'unsaved');
  } finally { ui.dom.window.close(); }
});

test('stale clean save reloads the visible editor before later edits can be saved', async () => {
  const ui = await setup({ 'Note.md': Buffer.from('Original') });
  try {
    await ui.open(); await ui.select('Note.md');
    ui.disk.set('Note.md', Buffer.from('Outside'));
    ui.document.querySelector('#save').click(); await ui.pause();
    assert.match(ui.document.querySelector('#error').textContent, /changed on disk/i);
    assert.equal(ui.document.querySelector('.cm-line').textContent, 'Outside');
    assert.equal(ui.document.querySelector('#dirty').hidden, true);
    await ui.type('Edited outside');
    ui.document.querySelector('#save').click(); await ui.pause();
    assert.equal(ui.disk.get('Note.md').toString(), 'Edited outside');
  } finally { ui.dom.window.close(); }
});

test('outside change offers reload, keep mine, and unsaved editable merge markers', async () => {
  const ui = await setup({ 'Note.md': Buffer.from('Base') });
  try {
    await ui.open(); await ui.select('Note.md'); await ui.type('Mine');
    ui.disk.set('Note.md', Buffer.from('Outside'));
    ui.dom.window.dispatchEvent(new ui.dom.window.Event('focus'));
    await ui.pause();
    assert.equal(ui.document.querySelector('#conflict').hidden, false);
    assert.equal(ui.document.querySelector('.cm-line').textContent, 'Mine');
    ui.document.querySelector('#save').click(); await ui.pause();
    assert.match(ui.document.querySelector('#error').textContent, /changed on disk/i);
    assert.equal(ui.disk.get('Note.md').toString(), 'Outside');
    ui.document.querySelector('#merge').click(); await ui.pause();
    assert.equal(ui.document.querySelector('#conflict').hidden, true);
    assert.match(ui.document.querySelector('.cm-content').textContent, /<<<<<<< mine.*Mine.*=======.*Outside.*>>>>>>> outside/s);
    assert.equal(ui.document.querySelector('.cm-content').getAttribute('contenteditable'), 'true');
    assert.equal(ui.document.querySelector('#dirty').hidden, false);
    assert.equal(ui.disk.get('Note.md').toString(), 'Outside');
    await ui.type('Reviewed');
    assert.equal(ui.document.querySelector('#dirty').hidden, false);
    assert.equal(ui.disk.get('Note.md').toString(), 'Outside');
  } finally { ui.dom.window.close(); }
});

test('CRLF and CR merged markers stay editable without changing untouched line endings on save', async () => {
  for (const separator of ['\r\n', '\r']) {
    const ui = await setup({ 'Note.md': Buffer.from(`Base${separator}Line`) });
    try {
      await ui.open(); await ui.select('Note.md'); await ui.type('Mine');
      ui.disk.set('Note.md', Buffer.from(`Outside${separator}Line`));
      ui.dom.window.dispatchEvent(new ui.dom.window.Event('focus')); await ui.pause();
      ui.document.querySelector('#merge').click(); await ui.pause();
      assert.equal(ui.document.querySelector('.cm-content').getAttribute('contenteditable'), 'true');
      assert.equal(ui.document.querySelector('#save').disabled, false);
      const line = ui.document.querySelectorAll('.cm-line')[1];
      line.textContent = 'Reviewed';
      ui.document.querySelector('.cm-content').dispatchEvent(new ui.dom.window.Event('input', { bubbles: true }));
      await ui.pause();
      ui.document.querySelector('#save').click(); await ui.pause();
      assert.deepEqual(ui.disk.get('Note.md'), Buffer.from(
        `<<<<<<< mine${separator}Reviewed${separator}Line${separator}=======${separator}Outside${separator}Line${separator}>>>>>>> outside`));
    } finally { ui.dom.window.close(); }
  }
});

test('keep mine writes only reported outside version, and reload discards edits explicitly', async () => {
  const ui = await setup({ 'Note.md': Buffer.from('Base') });
  try {
    await ui.open(); await ui.select('Note.md'); await ui.type('Mine');
    ui.disk.set('Note.md', Buffer.from('Outside'));
    ui.dom.window.dispatchEvent(new ui.dom.window.Event('focus')); await ui.pause();
    ui.disk.set('Note.md', Buffer.from('Newer outside'));
    ui.document.querySelector('#keep-mine').click(); await ui.pause();
    assert.match(ui.document.querySelector('#error').textContent, /changed again/i);
    assert.equal(ui.document.querySelector('.cm-line').textContent, 'Mine');
    assert.equal(ui.disk.get('Note.md').toString(), 'Newer outside');
    ui.dom.window.dispatchEvent(new ui.dom.window.Event('focus')); await ui.pause();
    ui.document.querySelector('#keep-mine').click(); await ui.pause();
    assert.equal(ui.disk.get('Note.md').toString(), 'Mine');
    assert.equal(ui.document.querySelector('#dirty').hidden, true);
    await ui.type('Draft'); ui.disk.set('Note.md', Buffer.from('Reloaded'));
    ui.dom.window.dispatchEvent(new ui.dom.window.Event('focus')); await ui.pause();
    ui.document.querySelector('#reload').click(); await ui.pause();
    assert.equal(ui.document.querySelector('.cm-line').textContent, 'Reloaded');
    assert.equal(ui.document.querySelector('#dirty').hidden, true);
  } finally { ui.dom.window.close(); }
});

test('missing note retains edits and refuses to overwrite a reappeared file', async () => {
  const ui = await setup({ 'Note.md': Buffer.from('Base') });
  try {
    await ui.open(); await ui.select('Note.md'); await ui.type('Recover');
    ui.disk.delete('Note.md');
    ui.dom.window.dispatchEvent(new ui.dom.window.Event('focus')); await ui.pause();
    assert.equal(ui.document.querySelector('#missing').hidden, false);
    assert.equal(ui.document.querySelector('.cm-line').textContent, 'Recover');
    ui.disk.set('Note.md', Buffer.from('Reappeared'));
    ui.document.querySelector('#save-as-new').click(); await ui.pause();
    assert.match(ui.document.querySelector('#error').textContent, /already exists/i);
    assert.equal(ui.document.querySelector('.cm-line').textContent, 'Recover');
    assert.equal(ui.disk.get('Note.md').toString(), 'Reappeared');
    ui.disk.delete('Note.md');
    ui.document.querySelector('#save-as-new').click(); await ui.pause();
    assert.equal(ui.disk.get('Note.md').toString(), 'Recover');
    assert.equal(ui.document.querySelector('#dirty').hidden, true);
    await ui.type('Throw away'); ui.disk.delete('Note.md');
    ui.dom.window.dispatchEvent(new ui.dom.window.Event('focus')); await ui.pause();
    ui.document.querySelector('#discard').click(); await ui.pause();
    assert.equal(ui.document.querySelector('#missing').hidden, true);
    ui.dom.window.dispatchEvent(new ui.dom.window.Event('focus')); await ui.pause();
    assert.equal(ui.document.querySelector('#missing').hidden, true);
    assert.equal(ui.document.querySelector('#dirty').hidden, true);
    assert.equal(ui.document.querySelector('.cm-line').textContent, 'Recover');
    await ui.type('Recovery again');
    ui.dom.window.dispatchEvent(new ui.dom.window.Event('focus')); await ui.pause();
    assert.equal(ui.document.querySelector('#missing').hidden, false);
  } finally { ui.dom.window.close(); }
});

test('note actions require explicit move and trash choices and preserve errors and unsaved text', async () => {
  const ui = await setup({ 'Source.md': Buffer.from('[[Target]]'), 'Target.md': Buffer.from('Target'), 'image.png': Buffer.from('image') });
  try {
    await ui.open(); await ui.select('Source.md');
    ui.document.querySelector('#new-path').value = 'Target';
    ui.document.querySelector('#create-form').requestSubmit(); await ui.pause();
    assert.match(ui.document.querySelector('#error').textContent, /already exists/i);
    ui.document.querySelector('#new-path').value = 'New';
    ui.document.querySelector('#create-form').requestSubmit(); await ui.pause();
    assert.equal(ui.document.querySelector('#title').textContent, 'New.md');
    assert.equal(ui.disk.get('New.md').toString(), '');
    ui.document.querySelector('#move').click();
    ui.document.querySelector('#move-path').value = 'Moved';
    ui.document.querySelector('#move-path').dispatchEvent(new ui.dom.window.KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
    assert.equal(ui.document.querySelector('#move-choice').hidden, true);
    assert.equal(ui.calls.filter((call) => call.operation === 'move').length, 0);
    ui.document.querySelector('#move').click();
    ui.document.querySelector('#move-path').value = 'Moved';
    ui.document.querySelector('#move-path').dispatchEvent(new ui.dom.window.KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
    await ui.pause();
    assert.deepEqual(JSON.parse(JSON.stringify(ui.calls.find((call) => call.operation === 'move'))),
      { operation: 'move', path: 'Moved', updateLinks: true });
    assert.equal(ui.document.querySelector('#title').textContent, 'Moved.md');
    for (const id of ['move', 'trash', 'follow', 'open-attachment']) {
      assert.ok(ui.document.querySelector(`#${id}`).textContent.trim(), `${id} has an accessible name`);
    }
    ui.document.querySelector('#trash').click();
    ui.document.querySelector('#trash-cancel').click();
    assert.equal(ui.calls.filter((call) => call.operation === 'trash').length, 0);
    ui.document.querySelector('#trash').click();
    ui.document.querySelector('#trash-confirm').click(); await ui.pause();
    assert.match(ui.document.querySelector('#error').textContent, /trash unavailable/i);
    assert.ok(ui.disk.has('Moved.md'));
    await ui.type('Unsaved');
    ui.document.querySelector('#trash').click();
    ui.document.querySelector('#trash-confirm').click(); await ui.pause();
    assert.match(ui.document.querySelector('#error').textContent, /Unsaved changes/i);
    assert.equal(ui.document.querySelector('.cm-line').textContent, 'Unsaved');
    ui.document.querySelector('#save').click(); await ui.pause();
    ui.enableTrash();
    ui.document.querySelector('#trash').click();
    ui.document.querySelector('#trash-confirm').click(); await ui.pause();
    assert.equal(ui.disk.has('Moved.md'), false);
    assert.equal(ui.document.querySelector('#title').textContent, 'No note selected');
  } finally { ui.dom.window.close(); }
});

test('note action errors without a snapshot leave the editor unlocked', async () => {
  const ui = await setup();
  try {
    await ui.open(); await ui.select('BOM.md');
    ui.injectError('Could not serialize vault state.');
    ui.document.querySelector('#move').click();
    ui.document.querySelector('#move-path').value = 'Moved';
    ui.document.querySelector('#move-without-links').click(); await ui.pause();
    assert.match(ui.document.querySelector('#error').textContent, /serialize vault state/);
    assert.equal(ui.document.querySelector('#title').textContent, 'BOM.md');
    assert.equal(ui.document.querySelector('#save').disabled, false);
    await ui.type('Still editable');
    assert.equal(ui.document.querySelector('#dirty').hidden, false);
  } finally { ui.dom.window.close(); }
});

test('follow note and attachment links only through bridge, with failed links retaining selection', async () => {
  const ui = await setup({ 'Source.md': Buffer.from('[[Target]] [[image.png]]'),
    'Target.md': Buffer.from('Target'), 'image.png': Buffer.from('image') });
  try {
    await ui.open(); await ui.select('Source.md');
    ui.document.querySelector('#link').value = 'image.png';
    ui.document.querySelector('#open-attachment').click(); await ui.pause();
    assert.deepEqual(ui.opened, ['image.png']);
    ui.document.querySelector('#link').value = '../outside';
    ui.document.querySelector('#open-attachment').click(); await ui.pause();
    assert.match(ui.document.querySelector('#error').textContent, /not found/i);
    ui.document.querySelector('#link').value = 'Target';
    ui.document.querySelector('#follow').click(); await ui.pause();
    assert.equal(ui.document.querySelector('#title').textContent, 'Target.md');
    await ui.type('Mine');
    ui.document.querySelector('#link').value = 'image.png';
    ui.document.querySelector('#open-attachment').click(); await ui.pause();
    assert.match(ui.document.querySelector('#error').textContent, /Unsaved changes/i);
    assert.deepEqual(ui.opened, ['image.png']);
  } finally { ui.dom.window.close(); }
});

test('rejected edit cannot silently switch or save unsynchronized text', async () => {
  const ui = await setup();
  try {
    await ui.open(); await ui.select('BOM.md');
    ui.injectError('Edit too large.');
    await ui.type('local only');
    assert.match(ui.document.querySelector('#error').textContent, /Edit too large/);
    await ui.select('Other.md');
    assert.equal(ui.document.querySelector('#title').textContent, 'BOM.md');
    assert.equal(ui.document.querySelector('.cm-line').textContent, 'local only');
    ui.document.querySelector('#save').click(); await ui.pause();
    assert.equal(ui.calls.at(-1).operation, 'edit');
    assert.match(ui.document.querySelector('#error').textContent, /not accepted by the bridge/);
  } finally { ui.dom.window.close(); }
});
