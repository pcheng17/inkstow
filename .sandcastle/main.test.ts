import assert from 'node:assert/strict';
import { test } from 'node:test';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import type { Sandbox } from '@ai-hero/sandcastle';
import { GitHubIssueTracker } from './github.js';
import { pi } from '@ai-hero/sandcastle';
import { SANDBOX_PI_MODEL } from './auth.js';
import { z } from 'zod';
import { assertBranchFree, branchForIssue, mustExec, parseTagged, publishPR, pushIsolated, readOnly, readOnlyAgent, runWorkflow, verify } from './main.mjs';
import { clearCheckpoint, newCheckpoint } from './checkpoint.js';

test('branchForIssue uses a stable issue-specific branch and rejects existing local or remote branches', () => {
  const branch = branchForIssue(42);
  assert.equal(branch, 'sandcastle/issue-42');
  const calls: string[][] = [];
  assertBranchFree(branch, (...args) => { calls.push(args); return ''; });
  assert.deepEqual(calls, [['branch', '--list', branch], ['ls-remote', '--heads', 'origin', branch]]);
  for (const match of ['branch', 'ls-remote']) {
    assert.throws(() => assertBranchFree(branch, (...args) => args[0] === match ? branch : ''), /already exists/);
  }
});

test('pushIsolated ignores a sandbox-changed origin and URL rewrite', () => {
  const root = mkdtempSync(join(tmpdir(), 'inkstow-push-test-'));
  const source = join(root, 'source');
  const target = join(root, 'target.git');
  const evil = join(root, 'evil.git');
  const run = (...args: string[]) => execFileSync('git', args, { encoding: 'utf8' }).trim();
  try {
    run('init', '-q', source);
    run('init', '-q', '--bare', target);
    run('init', '-q', '--bare', evil);
    run('-C', source, 'config', 'user.name', 'Test');
    run('-C', source, 'config', 'user.email', 'test@example.com');
    writeFileSync(join(source, 'README.md'), 'committed\n');
    run('-C', source, 'add', 'README.md');
    run('-C', source, 'commit', '-qm', 'change');
    run('-C', source, 'remote', 'add', 'origin', evil);
    run('-C', source, 'config', `url.file://${evil}.insteadOf`, `file://${target}`);
    pushIsolated(source, 'sandcastle/issue-42', `file://${target}`);
    assert.equal(run('-C', target, 'rev-parse', 'refs/heads/sandcastle/issue-42'), run('-C', source, 'rev-parse', 'HEAD'));
    assert.equal(run('-C', evil, 'branch', '--list', 'sandcastle/issue-42'), '');
  } finally { rmSync(root, { recursive: true, force: true }); }
});

test('publishPR fast-forwards the verified branch before reusing an existing open PR', () => {
  const calls: string[] = [];
  const url = publishPR('/tmp/worktree', 'sandcastle/issue-42', 'title', 'body',
    (_worktree, branch) => { calls.push(`push:${branch}`); },
    (_command, args) => { calls.push(`gh:${args[1]}`); return JSON.stringify([
      { headRefName: 'sandcastle/issue-42', headRepositoryOwner: { login: 'pcheng17' }, state: 'OPEN', url: 'https://example.test/pr/42' },
    ]); });
  assert.equal(url, 'https://example.test/pr/42');
  assert.deepEqual(calls, ['gh:list', 'push:sandcastle/issue-42']);
  assert.throws(() => publishPR('/tmp/worktree', 'sandcastle/issue-42', 'title', 'body',
    () => { throw new Error('remote diverged'); },
    () => JSON.stringify([{ headRefName: 'sandcastle/issue-42', headRepositoryOwner: { login: 'pcheng17' }, state: 'OPEN', url: 'https://example.test/pr/42' }])), /remote diverged/);
});

test('parseTagged validates the last JSON block with Zod', () => {
  const schema = z.object({ status: z.literal('done') });
  assert.deepEqual(parseTagged('<result>{}</result>\n<result>{"status":"done"}</result>', 'result', schema), { status: 'done' });
  assert.throws(() => parseTagged('<result>{}</result>', 'result', schema));
  assert.throws(() => parseTagged('no tag', 'result', schema), /Missing/);
});

test('readOnlyAgent grants only read, disables extensions and MCP, and keeps explicit model', () => {
  const provider = readOnlyAgent(pi(SANDBOX_PI_MODEL));
  const command = provider.buildPrintCommand({ prompt: 'inspect', dangerouslySkipPermissions: true }).command;
  assert.match(command, /--model 'openai\/gpt-6-sol'/);
  assert.match(command, /--tools read/);
  assert.match(command, /--no-extensions --no-mcp/);
  assert.doesNotMatch(command, /--tools bash/);
});

test('readOnly rejects HEAD or worktree changes even if the pass throws', async () => {
  let head = 'old';
  let dirty = '';
  const sandbox = { exec: async (command: string) => ({ stdout: command.includes('rev-parse') ? head : dirty, stderr: '', exitCode: 0 }) };
  assert.equal(await readOnly(sandbox, 'review', async () => 'good'), 'good');
  await assert.rejects(readOnly(sandbox, 'review', async () => { head = 'new'; throw new Error('agent error'); }), /modified HEAD/);
  head = 'old';
  await assert.rejects(readOnly(sandbox, 'PR', async () => { dirty = ' M README.md'; }), /modified HEAD or working tree/);
});

test('verify fails closed on dirty tree, absent commits, failed build and C++ checks', async () => {
  const commands: string[] = [];
  let status = '';
  let commits = '1';
  let fail = '';
  const sandbox = { exec: async (command: string) => {
    commands.push(command);
    if (command === 'git status --porcelain') return { stdout: status, stderr: '', exitCode: 0 };
    if (command.startsWith('git rev-list')) return { stdout: commits, stderr: '', exitCode: 0 };
    if (command.startsWith('git diff --name-only')) return { stdout: 'src/VaultInspect.cpp', stderr: '', exitCode: 0 };
    return { stdout: 'failure output', stderr: '', exitCode: command.includes(fail) && fail ? 2 : 0 };
  } };
  status = ' M src/VaultInspect.cpp';
  assert.match((await verify(sandbox, 'abc'))!, /not clean/);
  status = '';
  commits = '0';
  assert.match((await verify(sandbox, 'abc'))!, /No committed changes/);
  commits = '1';
  fail = 'cmake --build';
  assert.match((await verify(sandbox, 'abc'))!, /cmake --build.*failed/);
  assert.ok(!commands.some(command => command.includes('ctest --preset')));
  commands.length = 0;
  fail = 'check-clang-tidy.py';
  assert.match((await verify(sandbox, 'abc'))!, /check-clang-tidy.py.*failed/);
  assert.ok(commands.some(command => command.includes('clang-format --dry-run --Werror')));
  assert.ok(commands.some(command => command.includes('ctest --preset debug')));
  assert.ok(commands.some(command => command.includes('cmake --preset debug')));
  assert.ok(commands.some(command => command.includes('--target inkstow-webview unit-tests vault-inspect')));
  await assert.rejects(mustExec({ exec: async () => ({ stdout: '', stderr: 'error', exitCode: 1 }) }, 'broken'), /failed/);
});

test('runWorkflow uses one implementer session, fresh serial read-only passes, and host-only PR/comment', async () => {
  const repo = mkdtempSync(join(tmpdir(), 'inkstow-workflow-'));
  const git = (...args: string[]) => execFileSync('git', ['-C', repo, ...args], { encoding: 'utf8' }).trim();
  try {
    git('init', '-q');
    git('config', 'user.name', 'Test');
    git('config', 'user.email', 'test@example.com');
    writeFileSync(join(repo, 'README.md'), 'base\n');
    git('add', 'README.md');
    git('commit', '-qm', 'base');
    const base = git('rev-parse', 'HEAD');
    writeFileSync(join(repo, 'README.md'), 'changed\n');
    git('commit', '-qam', 'change');
    const stages: string[] = [];
    const result = (stdout: string, next?: (text: string) => Promise<unknown>) => ({
      stdout, resume: next, iterations: [{ sessionId: 'session-42' }],
    });
    const implementer = result('<implementation>{"status":"done","summary":"updated","noticed":[]}</implementation>', async text => {
      stages.push('resume:' + text.slice(0, 15));
      return result('<noticed>[]</noticed>', async () => result('fixed'));
    });
    let pushCount = 0;
    let comments = 0;
    const tracker = new GitHubIssueTracker(args => {
      if (args[0] === 'issue' && args[1] === 'view') return JSON.stringify({
        number: 42, title: 'Change', body: 'Update', url: 'https://example.test/42',
        state: 'OPEN', labels: [{ name: 'agent:running' }], comments: [], parent: null,
      });
      if (args[0] === 'api') return '[[]]';
      assert.equal(args[0], 'issue');
      if (args[1] === 'comment') comments++;
      return '';
    });
    const sandbox = {
      worktreePath: repo,
      exec: async (command: string) => ({
        stdout: command.startsWith('git rev-parse') ? git('rev-parse', 'HEAD')
          : command.startsWith('git status') ? git('status', '--porcelain')
          : command.startsWith('git rev-list') ? '1'
          : command.startsWith('git diff --name-only') ? '' : '',
        stderr: '', exitCode: 0,
      }),
      run: async ({ name, agent }: { name: string; agent: { buildPrintCommand: (options: { prompt: string; dangerouslySkipPermissions: boolean }) => { command: string } } }) => {
        stages.push(name);
        if (name === 'implement') return implementer;
        if (name !== 'simplify') assert.match(agent.buildPrintCommand({ prompt: '', dangerouslySkipPermissions: true }).command, /--tools read/);
      if (name === 'simplify') return result('<noticed>[]</noticed>');
        if (name === 'pr') return result('<pr>{"title":"feat: updated","body":"Validated."}</pr>');
        return result('<review>{"blocking":[],"nonBlocking":[]}</review>');
      },
    } as unknown as Sandbox;
    const ticket = { number: 42, title: 'Change', body: 'Update', url: 'https://example.test/42', comments: [], parentSpec: null };
    await runWorkflow(ticket, sandbox, base, pi(SANDBOX_PI_MODEL), tracker, (branch, title, body) => {
      assert.equal(branch, 'sandcastle/issue-42');
      assert.equal(title, 'feat: updated');
      assert.match(body, /Closes #42/);
      pushCount++;
      return 'https://example.test/pr/1';
    });
    assert.equal(pushCount, 1);
    assert.equal(comments, 1);
    assert.deepEqual(stages, ['implement', 'simplify', 'standards-0', 'spec-0', 'pr']);
  } finally { rmSync(repo, { recursive: true, force: true }); }
});

test('published PR checkpoint survives a failed review-label update and can be reconciled', async () => {
  const ticket = { number: 78, title: 'Finish', body: 'Done', url: 'https://example.test/78', comments: [], parentSpec: null };
  const state = newCheckpoint(ticket, 'base', 'sandcastle/issue-78');
  state.phase = 'publish';
  state.pr = { title: 'Finish', body: 'Done' };
  let label = 'agent:running';
  let attempts = 0;
  const tracker = new GitHubIssueTracker(args => {
    if (args[1] === 'view') return JSON.stringify({ ...ticket, state: 'OPEN', labels: [{ name: label }], comments: [], parent: null });
    if (args[0] === 'api') return '[[]]';
    if (args[1] === 'edit') {
      attempts++;
      if (attempts === 1) throw new Error('temporarily unavailable');
      label = args[args.indexOf('--add-label') + 1];
    }
    return '';
  });
  const sandbox = { exec: async (command: string) => ({
    stdout: command.startsWith('git rev-list') ? '1' : '', stderr: '', exitCode: 0,
  }), run: async () => { throw new Error('publish recovery must not rerun agents'); } } as unknown as Sandbox;
  let published = 0;
  const publish = () => { published++; return 'https://example.test/pr/78'; };
  try {
    await assert.rejects(runWorkflow(ticket, sandbox, 'base', pi(SANDBOX_PI_MODEL), tracker, publish, state, () => {}), /gh issue edit failed/);
    assert.equal(state.phase, 'publish');
    assert.equal(label, 'agent:review');
    assert.equal((await tracker.getResumeTicket(78, 'publish')).number, 78);
    tracker.setAgentStatus(78, 'running'); // main claims a resumed run
    await runWorkflow(ticket, sandbox, 'base', pi(SANDBOX_PI_MODEL), tracker, publish, state, () => {});
    assert.equal(label, 'agent:review');
    assert.equal(published, 2); // publishPR reconciles the existing PR instead of creating another
  } finally { clearCheckpoint(78); }
});

test('runWorkflow marks a blocked implementation agent:blocked without pushing or opening a PR', async () => {
  const calls: string[][] = [];
  const tracker = new GitHubIssueTracker(args => { calls.push([...args]); return ''; });
  const ticket = { number: 42, title: 'Blocked', body: '', url: 'https://example.test/42', comments: [], parentSpec: null };
  const sandbox = { run: async () => ({ stdout: '<implementation>{"status":"blocked","summary":"needs a decision","noticed":[]}</implementation>' }) } as unknown as Sandbox;
  await assert.rejects(runWorkflow(ticket, sandbox, 'base', pi(SANDBOX_PI_MODEL), tracker,
    () => { throw new Error('must not push'); }), /needs a decision/);
  assert.deepEqual(calls.map(args => args.slice(0, 2)), [['issue', 'edit'], ['issue', 'comment']]);
  assert.ok(calls[0].includes('agent:blocked'));
  clearCheckpoint(42);
});

test('exhausted reviewer findings leave a resumable handoff; explicit resume continues from the captured session', async () => {
  const repo = mkdtempSync(join(tmpdir(), 'inkstow-review-recovery-'));
  const git = (...args: string[]) => execFileSync('git', ['-C', repo, ...args], { encoding: 'utf8' }).trim();
  try {
    git('init', '-q');
    git('config', 'user.name', 'Test');
    git('config', 'user.email', 'test@example.com');
    writeFileSync(join(repo, 'note'), 'base');
    git('add', '.');
    git('commit', '-qm', 'base');
    const base = git('rev-parse', 'HEAD');
    writeFileSync(join(repo, 'note'), 'changed');
    git('commit', '-qam', 'change');
    const ticket = { number: 77, title: 'Fix', body: 'Fix note', url: 'https://example.test/77', comments: [], parentSpec: null };
    const state = newCheckpoint(ticket, base, 'sandcastle/issue-77');
    let reviewsBlock = true;
    const resumed: string[] = [];
    const messages: string[] = [];
    const tracker = new GitHubIssueTracker(args => {
      if (args[1] === 'view') return JSON.stringify({ ...ticket, state: 'OPEN', labels: [{ name: 'agent:running' }], comments: [], parent: null });
      if (args[0] === 'api') return '[[]]';
      if (args[1] === 'comment') messages.push(args.at(-1)!);
      return '';
    });
    const result = (stdout: string) => ({ stdout, iterations: [{ sessionId: 'captured-77' }] });
    const sandbox = {
      worktreePath: repo,
      exec: async (command: string) => ({
        stdout: command.startsWith('git rev-parse') ? git('rev-parse', 'HEAD')
          : command.startsWith('git rev-list') ? '1' : '', stderr: '', exitCode: 0,
      }),
      run: async ({ name, resumeSession }: { name: string; resumeSession?: string }) => {
        if (resumeSession) resumed.push(`${name}:${resumeSession}`);
        if (name === 'implement') return result('<implementation>{"status":"done","summary":"updated","noticed":[]}</implementation>');
        if (name === 'simplify') return result('<noticed>[]</noticed>');
        if (name.startsWith('standards') || name.startsWith('spec')) return result(`<review>{"blocking":${reviewsBlock ? '[{"axis":"spec","file":"note","finding":"fix missing case"}]' : '[]'},"nonBlocking":[]}</review>`);
        if (name === 'pr') return result('<pr>{"title":"Fix note","body":"Validated"}</pr>');
        return result('Fixed and committed.');
      },
    } as unknown as Sandbox;
    let pushed = 0;
    const publish = () => { pushed++; return 'https://example.test/pr/77'; };
    await assert.rejects(runWorkflow(ticket, sandbox, base, pi(SANDBOX_PI_MODEL), tracker, publish, state, () => {}), /Blocking review findings remain/);
    assert.equal(state.phase, 'fix');
    assert.equal(state.sessionId, 'captured-77');
    assert.match(messages[0], /fix missing case/);
    assert.match(messages[0], /--resume 77/);
    assert.equal(pushed, 0);
    reviewsBlock = false;
    state.rounds = 0;
    await runWorkflow(ticket, sandbox, base, pi(SANDBOX_PI_MODEL), tracker, publish, state, () => {});
    assert.ok(resumed.includes('fix:captured-77'));
    assert.equal(pushed, 1);
    assert.equal(messages.length, 2);
  } finally { clearCheckpoint(77); rmSync(repo, { recursive: true, force: true }); }
});
