import { execFileSync } from 'node:child_process';
import { z } from 'zod';

const REPO = 'pcheng17/inkstow';

const issueSchema = z.object({
  number: z.number().int().positive(),
  title: z.string(),
  body: z.string(),
  url: z.string(),
  state: z.enum(['OPEN', 'CLOSED']),
  labels: z.array(z.object({ name: z.string() })),
  comments: z.array(z.object({ body: z.string() })),
  parent: z.object({ number: z.number().int().positive() }).nullable().optional(),
});
const blockersSchema = z.array(z.array(z.object({
  number: z.number().int().positive(),
  state: z.enum(['open', 'closed']),
})));

type Issue = z.infer<typeof issueSchema>;

export interface Ticket {
  number: number;
  title: string;
  body: string;
  url: string;
  comments: string[];
  parentSpec: {
    number: number;
    title: string;
    body: string;
    url: string;
    comments: string[];
  } | null;
}

export type GhRunner = (args: readonly string[]) => string;

const defaultRunner: GhRunner = (args) =>
  execFileSync('gh', [...args], { encoding: 'utf8' });

function checkIssueNumber(number: number): void {
  if (!Number.isSafeInteger(number) || number < 1) {
    throw new RangeError('Issue number must be a positive safe integer');
  }
}

function checkComment(body: string): void {
  if (!body.trim()) throw new Error('Issue comment must not be empty');
}

export class TicketNotReadyError extends Error {
  constructor(
    public readonly reason: 'not-open' | 'missing-ready-for-agent' | 'open-blockers',
    public readonly blockerNumbers: number[] = [],
  ) {
    super(reason === 'open-blockers'
      ? `Issue has open blockers: ${blockerNumbers.join(', ')}`
      : `Issue is not ready: ${reason}`);
    this.name = 'TicketNotReadyError';
  }
}

/** Only the explicit mutation methods write to GitHub; eligibility checks are read-only. */
export class GitHubIssueTracker {
  constructor(private readonly runGh: GhRunner = defaultRunner) {}

  private run(args: string[]): string {
    try {
      return this.runGh(args);
    } catch (cause) {
      throw new Error(`gh ${args[0]} ${args[1]} failed`, { cause });
    }
  }

  private view(number: number): Issue {
    checkIssueNumber(number);
    return issueSchema.parse(JSON.parse(this.run([
      'issue', 'view', String(number), '--repo', REPO,
      '--json', 'number,title,body,url,state,labels,comments,parent',
    ])));
  }

  async getEligibleTicket(number: number): Promise<Ticket> {
    const issue = this.view(number);
    if (issue.state !== 'OPEN') throw new TicketNotReadyError('not-open');
    if (!issue.labels.some(({ name }) => name === 'ready-for-agent')) {
      throw new TicketNotReadyError('missing-ready-for-agent');
    }

    // --slurp keeps all pages as one JSON value, including when there are no blockers.
    const pages = blockersSchema.parse(JSON.parse(this.run([
      'api', `repos/${REPO}/issues/${number}/dependencies/blocked_by`,
      '--paginate', '--slurp',
    ])));
    const openBlockers = pages.flat().filter(({ state }) => state === 'open')
      .map(({ number: blockerNumber }) => blockerNumber);
    if (openBlockers.length) throw new TicketNotReadyError('open-blockers', openBlockers);

    const parent = issue.parent ? this.view(issue.parent.number) : null;
    return {
      number: issue.number,
      title: issue.title,
      body: issue.body,
      url: issue.url,
      comments: issue.comments.map(({ body }) => body),
      parentSpec: parent ? {
        number: parent.number,
        title: parent.title,
        body: parent.body,
        url: parent.url,
        comments: parent.comments.map(({ body }) => body),
      } : null,
    };
  }

  postIssueComment(number: number, body: string): void {
    checkIssueNumber(number);
    checkComment(body);
    this.run(['issue', 'comment', String(number), '--repo', REPO, '--body', body]);
  }

  markNeedsInfo(number: number, reason: string): void {
    checkIssueNumber(number);
    checkComment(reason);
    this.postIssueComment(number, reason);
    this.run([
      'issue', 'edit', String(number), '--repo', REPO,
      '--add-label', 'needs-info', '--remove-label', 'ready-for-agent',
    ]);
  }
}
