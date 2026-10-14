import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { JSDOM } from 'jsdom';

const html = await readFile(new URL('./index.html', import.meta.url), 'utf8');
const folder = '/tmp/inkstow-throwaway';
const encoder = new TextEncoder();
const decoder = new TextDecoder('utf-8', { fatal: true });

async function setup(initial = { 'BOM.md': Buffer.from([0xef, 0xbb, 0xbf, ...encoder.encode('Hello')]), 'Other.md': Buffer.from('Second') }) {
  const disk = new Map(Object.entries(initial));
  const calls = [];
  let notes = [];
  let path = '';
  let content = '';
  let saved = '';
  let bom = false;
  let injectedError = '';
  function snapshot(ok, error) {
    return { ok, notes: [...notes], path, content, dirty: content !== saved, ...(error ? { error } : {}) };
  }
  const dom = new JSDOM(html, {
    url: 'https://inkstow.invalid/', runScripts: 'dangerously', pretendToBeVisual: true,
    beforeParse(window) {
      window.ResizeObserver = class { observe() {} unobserve() {} disconnect() {} };
      window.Range.prototype.getClientRects = () => [];
      window.Range.prototype.getBoundingClientRect = () => ({ top: 0, bottom: 0, left: 0, right: 0, width: 0, height: 0 });
      window.HTMLElement.prototype.getClientRects = () => [];
      window.HTMLElement.prototype.getBoundingClientRect = () => ({ top: 0, bottom: 0, left: 0, right: 0, width: 0, height: 0 });
      window.inkstowRequest = async (request) => {
        calls.push(request);
        if (injectedError) { const message = injectedError; injectedError = ''; return snapshot(false, message); }
        switch (request.operation) {
          case 'open':
            if (request.folder !== folder) return snapshot(false, 'Vault folder not found.');
            if (content !== saved) return snapshot(false, 'Unsaved changes.');
            notes = [...disk.keys()].sort(); path = ''; content = saved = ''; return snapshot(true);
          case 'list': return snapshot(true);
          case 'select': {
            if (content !== saved) return snapshot(false, 'Unsaved changes.');
            if (!disk.has(request.path)) return snapshot(false, 'Note not found.');
            const bytes = disk.get(request.path);
            bom = bytes.subarray(0, 3).equals(Buffer.from([0xef, 0xbb, 0xbf]));
            path = request.path;
            content = saved = decoder.decode(bom ? bytes.subarray(3) : bytes);
            return snapshot(true);
          }
          case 'edit':
            if (!path) return snapshot(false, 'No note selected.');
            content = request.content;
            return snapshot(true);
          case 'save':
            if (!path) return snapshot(false, 'No note selected.');
            disk.set(path, Buffer.concat([bom ? Buffer.from([0xef, 0xbb, 0xbf]) : Buffer.alloc(0), Buffer.from(content)]));
            saved = content; return snapshot(true);
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
  return { dom, document, disk, calls, open, select, type, pause, injectError: (message) => { injectedError = message; } };
}

test('bundled editor has no remote runtime assets or links', () => {
  assert.match(html, /cm-editor/); // CodeMirror is bundled into the script, not imported at runtime.
  assert.doesNotMatch(html, /<script[^>]+src=|<link[^>]+href=|<a\s/i);
  assert.match(html, /connect-src 'none'/);
  const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
  assert.ok(html.includes(`script-src 'sha256-${createHash('sha256').update(script).digest('base64')}'`));
  assert.doesNotMatch(html, /script-src 'unsafe-inline'/);
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
