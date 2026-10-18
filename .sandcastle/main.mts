import { createSandbox, pi, type AgentProvider, type Sandbox, type SandboxRunResult } from '@ai-hero/sandcastle';
import { docker } from '@ai-hero/sandcastle/sandboxes/docker';
import { execFileSync, spawnSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { z } from 'zod';
import { ensureSandboxPiProfile, preflightSandboxPiAuth, sandboxPiAuthCheckCommand, sandboxPiLoginCommand, sandboxPiMount, SANDBOX_PI_MODEL } from './auth.js';
import { GitHubIssueTracker, TicketNotReadyError, type Ticket } from './github.js';
import { checkpointExists, clearCheckpoint, loadCheckpoint, newCheckpoint, saveCheckpoint, type Checkpoint } from './checkpoint.js';

const IMAGE = 'sandcastle:inkstow-pi';
const TIMEOUT = 1800;
const MAX_FIX_ROUNDS = 2;
const Implementation = z.object({ status: z.enum(['done', 'blocked']), summary: z.string(), noticed: z.array(z.string()) });
const Finding = z.object({ axis: z.enum(['standards', 'spec']), file: z.string(), finding: z.string() });
const Review = z.object({ blocking: z.array(Finding), nonBlocking: z.array(Finding) });
const Pr = z.object({ title: z.string().min(1), body: z.string().min(1) });
type Finding = z.infer<typeof Finding>;
export class Blocked extends Error {}
class IssueChanged extends Blocked {}

export function branchForIssue(number: number): string { return `sandcastle/issue-${number}`; }
export function quote(s: string): string { return `'${s.replaceAll("'", "'\\''")}'`; }
function git(...args: string[]): string { return execFileSync('git', args, { encoding: 'utf8' }).trim(); }
function host(command: string, args: string[]): string { return execFileSync(command, args, { encoding: 'utf8' }).trim(); }
/** Push from a fresh bare repository, never from the agent-writable worktree config. */
export function pushIsolated(worktree: string, branch: string, remote = 'https://github.com/pcheng17/inkstow.git'): void {
  const folder = mkdtempSync(join(tmpdir(), 'inkstow-push-'));
  try {
    const bare = join(folder, 'push.git');
    host('git', ['init', '--bare', '-q', bare]);
    host('git', ['-C', bare, '-c', 'protocol.file.allow=always', 'fetch', '--no-tags', worktree, 'HEAD']);
    host('git', ['-C', bare, '-c', 'core.hooksPath=/dev/null', '-c', 'credential.helper=!gh auth git-credential',
      'push', remote, `FETCH_HEAD:refs/heads/${branch}`]);
  } finally { rmSync(folder, { recursive: true, force: true }); }
}
export function publishPR(worktree: string, branch: string, title: string, body: string,
  push: (worktree: string, branch: string) => void = pushIsolated,
  run: (command: string, args: string[]) => string = host): string {
  const existing = (): string | undefined => {
    const prs = JSON.parse(run('gh', ['pr', 'list', '--repo', 'pcheng17/inkstow', '--state', 'all', '--head', branch,
      '--json', 'headRefName,headRepositoryOwner,url,state'])) as { headRefName: string; headRepositoryOwner: { login: string } | null; url: string; state: string }[];
    const previous = prs.find(pr => pr.headRefName === branch && pr.headRepositoryOwner?.login === 'pcheng17');
    if (previous && previous.state !== 'OPEN') throw new Blocked(`Existing PR ${previous.url} is ${previous.state}; inspect before resuming`);
    return previous?.url;
  };
  const previous = existing();
  const folder = mkdtempSync(join(tmpdir(), 'inkstow-pr-'));
  try {
    const bodyFile = join(folder, 'body.md');
    writeFileSync(bodyFile, body);
    // An existing PR may be behind local repairs. Always push the verified HEAD
    // (non-force), and refuse to publish if it is not a fast-forward.
    push(worktree, branch);
    if (previous) return previous;
    try { return run('gh', ['pr', 'create', '--repo', 'pcheng17/inkstow', '--base', 'main', '--head', branch, '--title', title, '--body-file', bodyFile]); }
    catch (error) { const created = existing(); if (created) return created; throw error; }
  } finally { rmSync(folder, { recursive: true, force: true }); }
}
function issueNumber(value: string | undefined): number {
  if (!value || !/^[1-9]\d*$/.test(value) || !Number.isSafeInteger(Number(value))) throw new Error('usage: npm run sandcastle -- [--check | --resume | --login] <issue-number>');
  return Number(value);
}
export function assertBranchFree(branch: string, runGit: (...args: string[]) => string): void {
  if (runGit('branch', '--list', branch) || runGit('ls-remote', '--heads', 'origin', branch)) {
    throw new Error(`Branch ${branch} already exists locally or on origin; finish or remove it before retrying.`);
  }
}
function prompt(name: string, args: Record<string, string>): string {
  let text = readFileSync(`.sandcastle/prompts/${name}.md`, 'utf8');
  for (const [key, value] of Object.entries(args)) text = text.replaceAll(`{{${key}}}`, value);
  return text;
}
export function parseTagged<T>(stdout: string, tag: string, schema: z.ZodType<T>): T {
  const raw = [...stdout.matchAll(new RegExp(`<${tag}>([\\s\\S]*?)</${tag}>`, 'g'))].at(-1)?.[1];
  if (!raw) throw new Error(`Missing <${tag}> output`);
  const parsed: unknown = JSON.parse(raw.trim().replace(/^```(?:json)?\s*\n/, '').replace(/\n```$/, ''));
  return schema.parse(parsed);
}
async function resume(result: SandboxRunResult, text: string): Promise<SandboxRunResult> {
  if (!result.resume) throw new Error('Pi session was not captured; cannot resume');
  return result.resume(text, { promptArgs: {}, idleTimeoutSeconds: TIMEOUT });
}
async function extract<T>(result: SandboxRunResult, tag: string, schema: z.ZodType<T>): Promise<{ value: T; session: SandboxRunResult }> {
  try { return { value: parseTagged(result.stdout, tag, schema), session: result }; } catch (error) {
    const corrected = await resume(result, `Your <${tag}> block was invalid (${String(error).slice(0, 500)}). Emit only a corrected <${tag}> JSON block.`);
    return { value: parseTagged(corrected.stdout, tag, schema), session: corrected };
  }
}
export async function mustExec(sandbox: Pick<Sandbox, 'exec'>, command: string): Promise<string> {
  const result = await sandbox.exec(command);
  if (result.exitCode) throw new Error(`${command} failed (exit ${result.exitCode}): ${result.stdout.slice(-3000)} ${result.stderr.slice(-3000)}`);
  return result.stdout.trim();
}

// Read-only Pi gets file inspection tools only; host provides the exact diff and commit snapshot.
export function readOnlyAgent(agent: AgentProvider): AgentProvider {
  return { ...agent, buildPrintCommand(options) {
    const cmd = agent.buildPrintCommand(options);
    return { ...cmd, command: `${cmd.command} --no-extensions --no-mcp --tools read` };
  } };
}
export async function readOnly<T>(sandbox: Pick<Sandbox, 'exec'>, name: string, action: () => Promise<T>): Promise<T> {
  const before = await mustExec(sandbox, 'git rev-parse HEAD');
  const status = await mustExec(sandbox, 'git status --porcelain');
  if (status) throw new Error(`Working tree dirty before ${name}: ${status}`);
  try { return await action(); } finally {
    const after = await mustExec(sandbox, 'git rev-parse HEAD');
    const dirty = await mustExec(sandbox, 'git status --porcelain');
    if (before !== after || dirty) throw new Error(`${name} modified HEAD or working tree; aborting`);
  }
}
async function checked(sandbox: Pick<Sandbox, 'exec'>, command: string): Promise<string | null> {
  const warnings = command.startsWith('cmake --build') ? 'if [ "$rc" -eq 0 ] && grep -Ei "(^|[[:space:]])warning:" /tmp/inkstow-check.log; then rc=1; fi; ' : '';
  const result = await sandbox.exec(`(${command}) >/tmp/inkstow-check.log 2>&1; rc=$?; ${warnings}tail -n 40 /tmp/inkstow-check.log; exit $rc`);
  return result.exitCode ? `${command} failed (exit ${result.exitCode}):\n${result.stdout.slice(-4000)}\n${result.stderr.slice(-1000)}` : null;
}
export async function verify(sandbox: Pick<Sandbox, 'exec'>, base: string): Promise<string | null> {
  const status = await mustExec(sandbox, 'git status --porcelain');
  if (status) return `Working tree is not clean:\n${status}`;
  if (await mustExec(sandbox, `git rev-list --count ${quote(base)}..HEAD`) === '0') return 'No committed changes';
  const changedCpp = await mustExec(sandbox, `git diff --name-only ${quote(base)}...HEAD -- '*.h' '*.cpp'`);
  const paths = changedCpp ? await mustExec(sandbox, `git diff --name-only --diff-filter=ACM ${quote(base)}...HEAD -- '*.h' '*.cpp'`) : '';
  const commands = [
    'cmake --preset debug',
    'cmake --build --preset debug --target inkstow-webview unit-tests vault-inspect --parallel 2',
    'ctest --preset debug',
  ];
  if (paths) commands.push(`clang-format --dry-run --Werror ${paths.split('\n').map(quote).join(' ')}`);
  if (changedCpp) commands.push('python3 scripts/check-clang-tidy.py');
  for (const command of commands) {
    console.log(`[host] Verification: ${command}`);
    const failure = await checked(sandbox, command);
    if (failure) return failure;
  }
  return null;
}
function formatFindings(items: Finding[]): string { return items.length ? items.map(f => `- [${f.axis}] ${f.file}: ${f.finding}`).join('\n') : '(none)'; }
export async function runWorkflow(ticket: Ticket, sandbox: Sandbox, base: string, agent: AgentProvider, tracker: GitHubIssueTracker, push: (branch: string, title: string, body: string) => string, state: Checkpoint = newCheckpoint(ticket, base, branchForIssue(ticket.number)), persist: (state: Checkpoint) => void = saveCheckpoint): Promise<void> {
  const branch = branchForIssue(ticket.number);
  const args = { ISSUE: String(ticket.number), ISSUE_JSON: JSON.stringify(ticket, null, 2), BASE_SHA: base, BRANCH: branch };
  const run = (name: string, text: string, provider = agent, sessionId?: string) => sandbox.run({ agent: provider, name, prompt: text, ...(sessionId ? { resumeSession: sessionId } : {}), idleTimeoutSeconds: TIMEOUT });
  const remember = (result: SandboxRunResult, phase: Checkpoint['phase']): void => {
    state.sessionId = result.iterations?.at(-1)?.sessionId;
    state.phase = phase;
    state.reason = undefined;
    persist(state);
  };
  const implement = async (name: string, text: string, phase: Checkpoint['phase']): Promise<SandboxRunResult> => {
    const result = await run(name, text, agent, state.sessionId);
    remember(result, phase);
    return result;
  };
  const reviewAgent = readOnlyAgent(agent);
  let prOpened = false;
  try {
    if (state.phase === 'implement') {
      const first = await run('implement', state.sessionId
        ? `Continue implementing issue #${ticket.number} on ${branch}. Check existing commits and worktree first. ${prompt('implement', args)}`
        : prompt('implement', args), agent, state.sessionId);
      // Capture the session even if the result needs a corrected JSON response.
      remember(first, 'implement');
      const initial = await extract(first, 'implementation', Implementation);
      remember(initial.session, 'implement');
      if (initial.value.status === 'blocked') throw new Blocked(initial.value.summary);
      state.summary = initial.value.summary;
      state.noticed = initial.value.noticed;
      state.phase = 'simplify';
      persist(state);
    }
    if (state.phase === 'simplify') {
      const result = await implement('simplify', prompt('simplify', args), 'simplify');
      const simplified = await extract(result, 'noticed', z.array(z.string()));
      state.noticed.push(...simplified.value);
      remember(simplified.session, 'verify');
    }
    if (state.phase === 'fix') {
      await implement('fix', `Review or verification findings:
${state.findings.join('\n')}\nFix on this branch, commit your change, and report what changed.`, 'verify');
    }
    if (state.phase === 'verify') {
      const verifyOrFix = async (): Promise<void> => {
        const failure = await verify(sandbox, base);
        if (!failure) return;
        state.findings = [failure];
        state.phase = 'fix';
        persist(state);
        await implement('verification-fix', `Verification failed:
${failure}\nFix on this branch and commit your change.`, 'verify');
        const again = await verify(sandbox, base);
        if (again) throw new Blocked(`Verification failed after correction:
${again}`);
      };
      await verifyOrFix();
      let nonBlocking: Finding[] = [];
      for (let round = state.rounds; ; round++) {
        const diff = host('git', ['-C', sandbox.worktreePath, 'diff', `${base}...HEAD`]);
        const commits = host('git', ['-C', sandbox.worktreePath, 'log', '--oneline', `${base}..HEAD`]);
        if (!diff) throw new Blocked('Empty diff after verification');
        const blocking: Finding[] = [];
        for (const axis of ['standards', 'spec'] as const) {
          const review = await readOnly(sandbox, `${axis} review`, async () => extract(
            await run(`${axis}-${round}`, prompt('review', { ...args, AXIS: axis, DIFF: diff, COMMITS: commits }), reviewAgent), 'review', Review));
          blocking.push(...review.value.blocking);
          nonBlocking.push(...review.value.nonBlocking);
        }
        if (!blocking.length) break;
        state.findings = blocking.map(f => `[${f.axis}] ${f.file}: ${f.finding}`);
        state.phase = 'fix';
        state.rounds = round + 1;
        persist(state);
        if (round === MAX_FIX_ROUNDS) throw new Blocked(`Blocking review findings remain:
${formatFindings(blocking)}`);
        await implement('fix', `Review findings:
${formatFindings(blocking)}\nFix on this branch, commit your change, and report what changed.`, 'verify');
        await verifyOrFix();
        nonBlocking = [];
      }
      const diff = host('git', ['-C', sandbox.worktreePath, 'diff', `${base}...HEAD`]);
      const pr = await readOnly(sandbox, 'PR body', async () => extract(
        await run('pr', prompt('pr', { ...args, DIFF: diff, SUMMARY: state.summary, NON_BLOCKING: formatFindings(nonBlocking), NOTICED: state.noticed.join('\n') || '(none)' }), reviewAgent), 'pr', Pr));
      state.pr = pr.value;
      state.phase = 'publish';
      persist(state);
    }
    if (!state.pr) throw new Error('Missing PR draft in publish checkpoint');
    const failure = await verify(sandbox, base);
    if (failure) {
      state.findings = [failure];
      state.phase = 'verify';
      persist(state);
      throw new Blocked(`Final verification failed:
${failure}`);
    }
    let current: Ticket;
    try { current = await tracker.getRunningTicket(ticket.number); }
    catch { throw new IssueChanged('Issue eligibility cannot be confirmed; re-triage before opening a PR'); }
    if (JSON.stringify(current) !== JSON.stringify(ticket)) {
      throw new IssueChanged('Issue changed during implementation; re-triage before opening a PR');
    }
    const url = push(branch, state.pr.title, `${state.pr.body}\n\nCloses #${ticket.number}\n`);
    prOpened = true;
    tracker.setAgentStatus(ticket.number, 'review');
    try { tracker.postIssueComment(ticket.number, `Sandcastle implemented on \`${branch}\`: ${url}`); }
    catch (commentError) { console.warn(`PR opened and labeled for review, but issue comment failed: ${String(commentError)}`); }
    console.log(`PR opened: ${url}`);
    clearCheckpoint(ticket.number);
  } catch (error) {
    const reason = error instanceof Error ? error.message.slice(0, 2500) : String(error);
    console.error(reason);
    state.reason = reason;
    persist(state);
    const handoff = `Sandcastle stopped on \`${branch}\` (${prOpened ? 'PR opened' : 'PR not confirmed'}). Phase: ${state.phase}. Base: ${base}. Session: ${state.sessionId ?? '(not captured)'}.\n\n${reason}\n\nResume with \`npm run sandcastle -- --resume ${ticket.number}\` after addressing the blocker. Full checkpoint: \`.sandcastle/state/issue-${ticket.number}.json\`. Logs: \`.sandcastle/logs/\`.`;
    if (!prOpened) {
      try { if (error instanceof IssueChanged) tracker.markBlockedIfRunning(ticket.number);
        else tracker.setAgentStatus(ticket.number, 'blocked'); }
      catch (updateError) { console.error(`Failed to mark issue blocked: ${String(updateError)}`); }
    }
    if (prOpened) {
      try { tracker.setAgentStatus(ticket.number, 'review'); }
      catch (updateError) { console.error(`Failed to mark PR ready for review: ${String(updateError)}`); }
    }
    try { tracker.postIssueComment(ticket.number, handoff); }
    catch (updateError) { console.error(`Failed to post recovery handoff: ${String(updateError)}`); }
    throw error;
  }
}

async function main(argv: string[]): Promise<void> {
  const mode = argv[0] === '--check' || argv[0] === '--login' || argv[0] === '--resume' ? argv.shift() : undefined;
  const number = mode === '--login' ? undefined : issueNumber(argv.shift());
  if (argv.length) throw new Error('Unexpected arguments');
  // Sandcastle automatically injects every key from this file into its container.
  // A local ignored file must never silently cross the sandbox credential boundary.
  if (existsSync('.sandcastle/.env')) throw new Error('Refusing .sandcastle/.env: Sandcastle would inject its contents into the sandbox');
  const profile = ensureSandboxPiProfile();
  if (mode === '--login') {
    const { command, args } = sandboxPiLoginCommand(profile, IMAGE, process.getuid!(), process.getgid!());
    const login = spawnSync(command, args, { stdio: 'inherit' });
    if (login.error || login.status !== 0) throw new Error(`Isolated Pi login failed: ${String(login.error ?? login.status)}`);
    return;
  }
  const tracker = new GitHubIssueTracker();
  const resumed = mode === '--resume';
  if (!resumed && checkpointExists(number!)) throw new Error(`Issue #${number} has a saved run; use --resume ${number}`);
  const state = resumed ? loadCheckpoint(number!) : undefined;
  let ticket: Ticket;
  try { ticket = resumed ? await tracker.getResumeTicket(number!, state!.phase) : await tracker.getEligibleTicket(number!); }
  catch (error) {
    if (mode !== '--check' && !resumed && error instanceof TicketNotReadyError && error.reason === 'open-blockers') {
      tracker.setAgentStatus(number!, 'blocked');
      try { tracker.postIssueComment(number!, `Sandcastle did not start: open blockers #${error.blockerNumbers.join(', #')}. When they close, relabel agent:ready and start a fresh run.`); }
      catch (updateError) { console.error(`Failed to post dependency handoff: ${String(updateError)}`); }
    }
    throw error;
  }
  const branch = branchForIssue(number!);
  if (!resumed) assertBranchFree(branch, git);
  if (resumed) {
    const localBranch = git('branch', '--list', branch);
    if (!localBranch && (state!.phase !== 'implement' || git('ls-remote', '--heads', 'origin', branch))) {
      throw new Error(`Saved branch ${branch} is not local; inspect or restore it before resuming`);
    }
    if (state!.branch !== branch || (localBranch && git('merge-base', state!.base, branch) !== state!.base)) {
      throw new Error('Saved branch diverged from checkpoint base; inspect it before resuming');
    }
  }
  git('fetch', 'origin', 'main');
  const base = state?.base ?? git('rev-parse', 'origin/main');
  if (mode === '--check') {
    const { command, args } = sandboxPiAuthCheckCommand(profile, IMAGE, process.getuid!(), process.getgid!());
    const check = spawnSync(command, args, { encoding: 'utf8' });
    let ready = false;
    try { ready = JSON.parse(check.stdout).status === 'ready'; } catch { /* fail closed */ }
    if (check.error || check.status !== 0 || !ready) throw new Error('Isolated Pi auth is not ready; run npm run sandcastle -- --login');
    console.log(`Ready: #${number} from ${base} on ${branch}; no issue/branch/PR created`);
    return;
  }
  const checkpoint = state ?? newCheckpoint(ticket, base, branch);
  if (resumed) {
    // Explicit resume accepts a human's updated issue/comments as the new spec.
    if (JSON.stringify(checkpoint.ticket) !== JSON.stringify(ticket) && checkpoint.phase === 'publish') checkpoint.phase = 'verify';
    checkpoint.ticket = ticket;
    checkpoint.rounds = 0;
  }
  checkpoint.reason = undefined;
  saveCheckpoint(checkpoint);
  const agent = pi(SANDBOX_PI_MODEL, { thinking: 'medium', sessionStorage: { hostSessionsDir: join(profile, 'sessions') } });
  let sandbox: Sandbox | undefined;
  try {
    sandbox = await createSandbox({ branch, baseBranch: base, sandbox: docker({ imageName: IMAGE, mounts: [sandboxPiMount(profile)], env: { GIT_CONFIG_COUNT: '1', GIT_CONFIG_KEY_0: 'core.hooksPath', GIT_CONFIG_VALUE_0: '.githooks' } }) });
    await preflightSandboxPiAuth(sandbox);
    for (const skill of ['tdd', 'code-simplification', 'code-review', 'diagnosing-bugs', 'codebase-design', 'dod', 'pr']) {
      await mustExec(sandbox, `test -f /home/agent/.agents/skills/${skill}/SKILL.md`);
    }
    tracker.setAgentStatus(number!, 'running');
    await runWorkflow(ticket, sandbox, base, agent, tracker,
      (head, title, body) => publishPR(sandbox!.worktreePath, head, title, body), checkpoint);
  } catch (error) {
    // Workflow failures already wrote a detailed handoff. Preflight failures need one too.
    if (!checkpoint.reason) {
      checkpoint.reason = String(error).slice(0, 2500);
      saveCheckpoint(checkpoint);
      try { tracker.setAgentStatus(number!, 'blocked'); }
      catch (updateError) { console.error(`Failed to mark issue blocked: ${String(updateError)}`); }
      try { tracker.postIssueComment(number!, `Sandcastle stopped on \`${branch}\` during setup: ${checkpoint.reason}. Resume with \`npm run sandcastle -- --resume ${number}\`. Checkpoint: \`.sandcastle/state/issue-${number}.json\`.`); }
      catch (updateError) { console.error(`Failed to post recovery handoff: ${String(updateError)}`); }
    }
    throw error;
  } finally { if (sandbox) {
    try { const closed = await sandbox.close(); if (closed.preservedWorktreePath) console.error(`Preserved worktree: ${closed.preservedWorktreePath}`); }
    catch (error) { console.error(`Sandbox cleanup failed; inspect ${sandbox.worktreePath}: ${String(error)}`); }
  } }
}

if (process.argv[1]?.endsWith('/main.mts')) {
  main(process.argv.slice(2)).catch(error => { console.error(String(error)); process.exitCode = 1; });
}
