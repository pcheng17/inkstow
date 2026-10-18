import { existsSync, mkdirSync, readFileSync, renameSync, rmSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { z } from 'zod';
import type { Ticket } from './github.js';

const ticketSchema = z.object({
  number: z.number().int().positive(), title: z.string(), body: z.string(), url: z.string(),
  comments: z.array(z.string()),
  parentSpec: z.object({ number: z.number().int().positive(), title: z.string(), body: z.string(), url: z.string(), comments: z.array(z.string()) }).nullable(),
});
const schema = z.object({
  version: z.literal(1), ticket: ticketSchema, base: z.string(), branch: z.string(),
  phase: z.enum(['implement', 'simplify', 'verify', 'fix', 'publish']),
  sessionId: z.string().optional(), summary: z.string().default(''), noticed: z.array(z.string()).default([]),
  findings: z.array(z.string()).default([]), rounds: z.number().int().nonnegative().default(0),
  pr: z.object({ title: z.string(), body: z.string() }).optional(),
  reason: z.string().optional(),
});
export type Checkpoint = z.infer<typeof schema>;
const dir = '.sandcastle/state';
function pathFor(number: number): string {
  if (!Number.isSafeInteger(number) || number < 1) throw new Error('Invalid issue number');
  return join(dir, `issue-${number}.json`);
}
export function newCheckpoint(ticket: Ticket, base: string, branch: string): Checkpoint {
  return { version: 1, ticket, base, branch, phase: 'implement', summary: '', noticed: [], findings: [], rounds: 0 };
}
export function loadCheckpoint(number: number): Checkpoint {
  const state = schema.parse(JSON.parse(readFileSync(pathFor(number), 'utf8')));
  if (state.ticket.number !== number || state.branch !== `sandcastle/issue-${number}`) throw new Error('Checkpoint does not match issue branch');
  return state;
}
export function checkpointExists(number: number): boolean { return existsSync(pathFor(number)); }
export function saveCheckpoint(state: Checkpoint): void {
  mkdirSync(dir, { recursive: true, mode: 0o700 });
  const path = pathFor(state.ticket.number);
  const temporary = `${path}.${process.pid}.tmp`;
  try {
    writeFileSync(temporary, JSON.stringify(schema.parse(state), null, 2) + '\n', { mode: 0o600 });
    renameSync(temporary, path);
  } finally { rmSync(temporary, { force: true }); }
}
export function clearCheckpoint(number: number): void { rmSync(pathFor(number), { force: true }); }
