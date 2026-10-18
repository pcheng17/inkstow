import assert from 'node:assert/strict';
import { test } from 'node:test';
import { GitHubIssueTracker, TicketNotReadyError, type GhRunner } from './github.js';

const issue = {
  number: 12,
  title: 'Implement feature',
  body: 'Ticket body',
  url: 'https://github.com/pcheng17/inkstow/issues/12',
  state: 'OPEN',
  labels: [{ name: 'agent:ready' }],
  comments: [{ body: 'A detail' }],
  parent: { number: 3 },
};
const parent = {
  ...issue,
  number: 3,
  title: 'Parent spec',
  body: 'Requirements',
  url: 'https://github.com/pcheng17/inkstow/issues/3',
  comments: [{ body: 'Spec amendment' }],
  parent: null,
};

function fakeGh(
  ticket: object = issue,
  blockers: object = [[{ number: 7, state: 'closed' }]],
  failApi = false,
): { tracker: GitHubIssueTracker; calls: string[][] } {
  const calls: string[][] = [];
  const run: GhRunner = (args) => {
    calls.push([...args]);
    if (args[0] === 'api') {
      if (failApi) throw new Error('GitHub unavailable');
      return JSON.stringify(blockers);
    }
    assert.equal(args[0], 'issue');
    assert.equal(args[1], 'view'); // A read-only fake: mutations always fail the test.
    return JSON.stringify(args[2] === '3' ? parent : ticket);
  };
  return { tracker: new GitHubIssueTracker(run), calls };
}

test('getEligibleTicket returns ticket, comments and parent spec with closed blockers', async () => {
  const { tracker, calls } = fakeGh();
  assert.deepEqual(await tracker.getEligibleTicket(12), {
    number: 12, title: 'Implement feature', body: 'Ticket body',
    url: issue.url, comments: ['A detail'],
    parentSpec: {
      number: 3, title: 'Parent spec', body: 'Requirements', url: parent.url,
      comments: ['Spec amendment'],
    },
  });
  assert.deepEqual(calls.map((args) => args.slice(0, 3)), [
    ['issue', 'view', '12'],
    ['api', 'repos/pcheng17/inkstow/issues/12/dependencies/blocked_by', '--paginate'],
    ['issue', 'view', '3'],
  ]);
});

test('getEligibleTicket accepts an unparented ticket without blockers', async () => {
  const { tracker } = fakeGh({ ...issue, parent: null }, [[]]);
  assert.equal((await tracker.getEligibleTicket(12)).parentSpec, null);
});

test('getEligibleTicket rejects closed and unlabelled tickets before querying dependencies', async () => {
  for (const [ticket, reason] of [
    [{ ...issue, state: 'CLOSED' }, 'not-open'],
    [{ ...issue, labels: [{ name: 'agent:blocked' }] }, 'missing-agent-label'],
  ] as const) {
    const { tracker, calls } = fakeGh(ticket);
    await assert.rejects(tracker.getEligibleTicket(12),
      (error: unknown) => error instanceof TicketNotReadyError && error.reason === reason);
    assert.equal(calls.length, 1);
  }
});

test('getEligibleTicket rejects open native blockers across pages', async () => {
  const { tracker } = fakeGh(issue, [
    [{ number: 7, state: 'closed' }],
    [{ number: 8, state: 'open' }, { number: 9, state: 'open' }],
  ]);
  await assert.rejects(tracker.getEligibleTicket(12), (error: unknown) => {
    if (!(error instanceof TicketNotReadyError)) return false;
    assert.equal(error.reason, 'open-blockers');
    assert.deepEqual(error.blockerNumbers, [8, 9]);
    return true;
  });
});

test('getEligibleTicket fails closed if the GitHub dependencies API fails', async () => {
  const { tracker, calls } = fakeGh(issue, [], true);
  await assert.rejects(tracker.getEligibleTicket(12), /gh api .* failed/);
  assert.equal(calls.length, 2);
});

test('getEligibleTicket fails closed if dependency state is missing', async () => {
  const { tracker } = fakeGh(issue, [[{ number: 8 }]]);
  await assert.rejects(tracker.getEligibleTicket(12), /Invalid option/);
});

test('lifecycle labels enforce one eligible state and resume only from running or blocked', async () => {
  for (const label of ['agent:running', 'agent:blocked']) {
    const { tracker } = fakeGh({ ...issue, parent: null, labels: [{ name: label }] }, [[]]);
    assert.equal((await tracker.getResumeTicket(12)).number, 12);
    await assert.rejects(tracker.getEligibleTicket(12), /missing-agent-label/);
  }
  const { tracker } = fakeGh({ ...issue, labels: [{ name: 'agent:ready' }, { name: 'agent:review' }] });
  await assert.rejects(tracker.getEligibleTicket(12), /missing-agent-label/);
  const reviewed = fakeGh({ ...issue, labels: [{ name: 'agent:review' }] });
  await assert.rejects(reviewed.tracker.getResumeTicket(12), /missing-agent-label/);
});

test('setAgentStatus swaps lifecycle labels in one issue edit', () => {
  const calls: string[][] = [];
  new GitHubIssueTracker(args => { calls.push([...args]); return ''; }).setAgentStatus(12, 'review');
  assert.deepEqual(calls.map(args => args.slice(0, 2)), [['issue', 'edit']]);
  assert.ok(calls[0].includes('agent:review'));
  assert.ok(calls[0].includes('agent:ready,agent:running,agent:blocked'));
});
