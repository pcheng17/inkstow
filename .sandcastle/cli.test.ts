import assert from 'node:assert/strict';
import { test } from 'node:test';
import { spawnSync } from 'node:child_process';
import { chmodSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

test('--check with open blockers leaves issue labels and comments untouched', () => {
  const folder = mkdtempSync(join(tmpdir(), 'inkstow-check-readonly-'));
  const script = join(folder, 'gh');
  const log = join(folder, 'gh.log');
  try {
    writeFileSync(script, `#!/bin/sh
printf '%s %s\\n' "$1" "$2" >> "$SC_TEST_GH_LOG"
if [ "$1" = issue ] && [ "$2" = view ]; then
  printf '%s\\n' '{"number":77,"title":"Blocked","body":"Spec","url":"https://example.test/77","state":"OPEN","labels":[{"name":"agent:ready"}],"comments":[],"parent":null}'
elif [ "$1" = api ]; then
  printf '%s\\n' '[[{"number":12,"state":"open"}]]'
else
  exit 2
fi
`);
    chmodSync(script, 0o700);
    const run = spawnSync(process.execPath, ['--import', 'tsx', '.sandcastle/main.mts', '--check', '77'], {
      encoding: 'utf8', env: { ...process.env, PATH: `${folder}:${process.env.PATH}`, SC_TEST_GH_LOG: log },
    });
    assert.equal(run.status, 1, run.stderr);
    assert.match(run.stderr, /open blockers/i);
    assert.deepEqual(readFileSync(log, 'utf8').trim().split('\n'), ['issue view', 'api repos/pcheng17/inkstow/issues/77/dependencies/blocked_by']);
  } finally { rmSync(folder, { recursive: true, force: true }); }
});
