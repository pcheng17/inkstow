import { build } from 'esbuild';
import { createHash } from 'node:crypto';
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const [source, header] = process.argv.slice(2);
if (!source || !header) throw new Error('Expected source directory and output header');
const { outputFiles } = await build({
  entryPoints: [join(source, 'main.ts')],
  nodePaths: [join(dirname(fileURLToPath(import.meta.url)), 'node_modules')],
  bundle: true,
  minify: true,
  write: false,
  format: 'iife',
  target: ['es2020'],
  logLevel: 'warning'
});
const template = await readFile(join(source, 'index.html'), 'utf8');
const css = await readFile(join(source, 'style.css'), 'utf8');
if (!template.includes('/* INKSTOW_JS */') || !template.includes('/* INKSTOW_CSS */') ||
    !template.includes('INKSTOW_SCRIPT_HASH')) {
  throw new Error('HTML asset markers missing');
}
const script = outputFiles[0].text.replace(/<\/script/gi, '<\\/script');
const hash = createHash('sha256').update(script).digest('base64');
const html = template.replace('INKSTOW_SCRIPT_HASH', () => hash)
  .replace('/* INKSTOW_CSS */', () => css)
  .replace('/* INKSTOW_JS */', () => script);
const delimiter = 'INKSTOW_HTML';
if (html.includes(`)${delimiter}"`)) throw new Error('HTML conflicts with C++ raw-string delimiter');
await mkdir(dirname(header), { recursive: true });
await writeFile(join(dirname(fileURLToPath(import.meta.url)), 'index.html'), html);
// MSVC limits individual string literals to 64 KiB; assemble the page from smaller chunks.
const chunks = Array.from(html.matchAll(/[\s\S]{1,8000}/gu), match => match[0]);
const literals = chunks.map(chunk => `  page += R"${delimiter}(${chunk})${delimiter}";\n`).join('');
await writeFile(header, `#pragma once\n#include <string>\n// Offline HTML generated from the pinned CodeMirror bundle.\ninline const std::string kLocalHtml = [] {\n  std::string page;\n  page.reserve(${Buffer.byteLength(html, 'utf8')});\n${literals}  return page;\n}();\n`);
