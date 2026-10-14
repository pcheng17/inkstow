import { lstatSync, mkdirSync, chmodSync } from 'node:fs';
import { homedir } from 'node:os';
import { join } from 'node:path';
import type { MountConfig } from '@ai-hero/sandcastle';

export const SANDBOX_PI_AGENT_PATH = '/home/agent/.pi/agent';
export const SANDBOX_PI_MODEL = 'openai-codex/gpt-6-sol';

/** Only this dedicated profile is shared with sandboxes; never mount the host's ~/.pi/agent. */
export function ensureSandboxPiProfile(home = homedir()): string {
  const profile = join(home, '.local/share/inkstow/sandcastle-pi/agent');
  mkdirSync(profile, { recursive: true, mode: 0o700 });
  const stat = lstatSync(profile);
  if (!stat.isDirectory() || stat.isSymbolicLink()) {
    throw new Error('Sandbox Pi profile must be a directory, not a symlink');
  }
  chmodSync(profile, 0o700);
  return profile;
}

export function sandboxPiMount(profile: string): MountConfig {
  return { hostPath: profile, sandboxPath: SANDBOX_PI_AGENT_PATH };
}

type SandboxAuthExecutor = {
  exec(command: string): Promise<{ stdout: string; stderr: string; exitCode: number }>;
};

/** Call before any ticket mutation; no --credentials flag or credential output is returned. */
export async function preflightSandboxPiAuth(sandbox: SandboxAuthExecutor): Promise<void> {
  const result = await sandbox.exec(`pi auth check --model ${SANDBOX_PI_MODEL} --json`);
  let status: unknown;
  try {
    const response: unknown = JSON.parse(result.stdout);
    status = response && typeof response === 'object' && 'status' in response
      ? response.status : undefined;
  } catch {
    throw new Error('Sandbox Pi auth check returned invalid JSON');
  }
  if (result.exitCode !== 0 || status !== 'ready') {
    throw new Error('Sandbox Pi auth is not ready; run the isolated login command first');
  }
}

/**
 * Run with spawnSync(command, args, { stdio: 'inherit' }) on the host and type /login in Pi.
 * This starts a standalone interactive container with only the dedicated profile mounted;
 * it does not mount the repository or forward host environment/gh credentials.
 */
export function sandboxPiLoginCommand(profile: string, imageName: string, uid: number, gid: number): {
  command: string;
  args: string[];
} {
  return {
    command: 'docker',
    args: [
      'run', '--rm', '-it', '--user', `${uid}:${gid}`,
      '--mount', `type=bind,src=${profile},dst=${SANDBOX_PI_AGENT_PATH}`,
      '--workdir', '/home/agent', '--entrypoint', 'pi', imageName,
      '--model', SANDBOX_PI_MODEL, '--no-session',
    ],
  };
}

/** No TTY flags: callers capture the JSON output for a fail-closed dry run. */
export function sandboxPiAuthCheckCommand(profile: string, imageName: string, uid: number, gid: number): {
  command: string;
  args: string[];
} {
  return {
    command: 'docker',
    args: [
      'run', '--rm', '--user', `${uid}:${gid}`,
      '--mount', `type=bind,src=${profile},dst=${SANDBOX_PI_AGENT_PATH}`,
      '--workdir', '/home/agent', '--entrypoint', 'pi', imageName,
      'auth', 'check', '--model', SANDBOX_PI_MODEL, '--json',
    ],
  };
}
