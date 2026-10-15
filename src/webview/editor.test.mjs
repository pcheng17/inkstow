import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { JSDOM } from 'jsdom';

const html = await readFile(new URL('./index.html', import.meta.url), 'utf8');
const folder = '/tmp/inkstow-throwaway';
const encoder = new TextEncoder();
const decoder = new TextDecoder('utf-8', { fatal: true });

async function setup(initial = { 'BOM.md': Buffer.from([0xef, 0xbb, 0xbf, ...encoder.encode('Hello')]), 'Other.md': Buffer.from('Second') }, startupError = '') {
  const disk = new Map(Object.entries(initial));
  const calls = [];
  let notes = [];
  let path = '';
  let content = '';
  let saved = '';
  let bom = false;
  let injectedError = '';
  let pendingSave;
  let exits = 0;
  let change = 'none';
  let dismissedMissing = false;
  let expectedBytes;
  let reportedBytes;
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
        if (injectedError) {
          const message = injectedError; injectedError = '';
          if (message === 'Clean reload failed save') { content = saved = 'Outside'; change = 'reloaded'; }
          return snapshot(false, message);
        }
        switch (request.operation) {
          case 'startup': return snapshot(!startupError, startupError);
          case 'pickVault':
            if (content !== saved) return snapshot(false, 'Unsaved changes.');
            notes = [...disk.keys()].sort(); path = ''; content = saved = ''; return snapshot(true);
          case 'open':
            if (request.folder !== folder) return snapshot(false, 'Vault folder not found.');
            if (content !== saved) return snapshot(false, 'Unsaved changes.');
            notes = [...disk.keys()].sort(); path = ''; content = saved = ''; return snapshot(true);
          case 'list': return snapshot(true);
          case 'follow':
            if (content !== saved) return snapshot(false, 'Save or discard your edits first.');
            if (request.link === 'image.png') return snapshot(true);
            return snapshot(false, 'Link could not be resolved.');
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
          case 'edit':
            if (!path) return snapshot(false, 'No note selected.');
            content = request.content;
            if (content !== saved) dismissedMissing = false;
            return snapshot(true);
          case 'refresh': {
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
  return { dom, document, disk, calls, open, select, type, pause, exits: () => exits,
    injectError: (message) => { injectedError = message; },
    delaySave: () => {
      let release;
      pendingSave = new Promise((resolve) => { release = resolve; });
      return release;
    } };
}

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
    ui.document.querySelector('#follow').click(); await ui.pause();
    assert.equal(ui.calls.at(-1).link, 'image.png');
    await ui.type('[[image.png]] more');
    ui.document.querySelector('#follow').click(); await ui.pause();
    assert.equal(ui.document.querySelector('#dirty').hidden, false);
    assert.match(ui.document.querySelector('#error').textContent, /Save or discard/);
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

test('Cancel during a pending Save and quit saves without exiting or reopening the prompt', async () => {
  const ui = await setup();
  try {
    await ui.open(); await ui.select('BOM.md'); await ui.type('Mine');
    const releaseSave = ui.delaySave();
    ui.dom.window.inkstowAskQuit(); await ui.pause();
    ui.document.querySelector('#quit-save').click(); await ui.pause();
    assert.equal(ui.calls.at(-1).operation, 'save');
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

test('focus refresh discovers outside folder changes without a selected note', async () => {
  const ui = await setup();
  try {
    await ui.open();
    ui.disk.set('Outside.md', Buffer.from('Outside'));
    ui.dom.window.dispatchEvent(new ui.dom.window.Event('focus'));
    await ui.pause();
    assert.deepEqual([...ui.document.querySelectorAll('#notes button')].map((b) => b.textContent),
      ['BOM.md', 'Other.md', 'Outside.md']);
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
