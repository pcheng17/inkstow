import assert from 'node:assert/strict';
import { lstatSync, mkdtempSync, readFileSync, rmSync, symlinkSync, writeFileSync, chmodSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { test } from 'node:test';
import {
  ensureSandboxPiProfile, preflightSandboxPiAuth, sandboxPiLoginCommand, sandboxPiMount,
  SANDBOX_PI_AGENT_PATH,
} from './auth.js';

test('ensureSandboxPiProfile creates a private persistent directory outside the repository', () => {
  const home = mkdtempSync(join(tmpdir(), 'inkstow-pi-test-'));
  try {
    const profile = ensureSandboxPiProfile(home);
    assert.equal(profile, join(home, '.local/share/inkstow/sandcastle-pi/agent'));
    assert.equal(lstatSync(profile).mode & 0o777, 0o700);
    writeFileSync(join(profile, 'sentinel'), 'preserved');
    chmodSync(profile, 0o755);
    assert.equal(ensureSandboxPiProfile(home), profile);
    assert.equal(lstatSync(profile).mode & 0o777, 0o700);
    assert.equal(readFileSync(join(profile, 'sentinel'), 'utf8'), 'preserved');
    assert.deepEqual(sandboxPiMount(profile), {
      hostPath: profile, sandboxPath: SANDBOX_PI_AGENT_PATH,
    });
  } finally {
    rmSync(home, { recursive: true, force: true });
  }
});

test('ensureSandboxPiProfile refuses a symlink profile', () => {
  const home = mkdtempSync(join(tmpdir(), 'inkstow-pi-test-'));
  try {
    const profile = ensureSandboxPiProfile(home);
    rmSync(profile, { recursive: true });
    symlinkSync(home, profile);
    assert.throws(() => ensureSandboxPiProfile(home), /not a symlink/);
  } finally {
    rmSync(home, { recursive: true, force: true });
  }
});

test('sandboxPiLoginCommand runs an isolated interactive container with the same mount', () => {
  const command = sandboxPiLoginCommand('/tmp/private pi', 'inkstow-pi:test', 1234, 2345);
  assert.equal(command.command, 'docker');
  assert.deepEqual(command.args, [
    'run', '--rm', '-it', '--user', '1234:2345',
    '--mount', `type=bind,src=/tmp/private pi,dst=${SANDBOX_PI_AGENT_PATH}`,
    '--workdir', '/home/agent', '--entrypoint', 'pi', 'inkstow-pi:test',
    '--model', 'openai-codex/gpt-6-sol', '--no-session',
  ]);
  assert.ok(!command.args.includes('--env'));
});

test('preflightSandboxPiAuth fails closed unless sandbox Pi reports ready', async () => {
  const calls: string[] = [];
  const sandbox = { exec: async (command: string) => {
    calls.push(command);
    return { stdout: '{"status":"ready"}', stderr: '', exitCode: 0 };
  } };
  await preflightSandboxPiAuth(sandbox);
  assert.deepEqual(calls, ['pi auth check --model openai-codex/gpt-6-sol --json']);
  for (const [stdout, exitCode] of [
    ['{"status":"not_ready"}', 1], ['{"status":"invalid"}', 2],
    ['{"status":"ready"}', 1], ['not JSON', 0], ['{}', 0],
  ] as const) {
    await assert.rejects(preflightSandboxPiAuth({ exec: async () => ({ stdout, stderr: '', exitCode }) }),
      /Sandbox Pi auth/);
  }
  await assert.rejects(preflightSandboxPiAuth({ exec: async () => { throw new Error('offline'); } }), /offline/);
});
