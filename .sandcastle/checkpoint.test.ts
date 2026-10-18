import assert from 'node:assert/strict';
import { test } from 'node:test';
import { checkpointExists, clearCheckpoint, loadCheckpoint, newCheckpoint, saveCheckpoint } from './checkpoint.js';

test('checkpoint survives a new invocation with its base, stage, and captured session', () => {
  const ticket = { number: 987654, title: 'Recover', body: 'spec', url: 'https://example.test', comments: [], parentSpec: null };
  const state = newCheckpoint(ticket, 'base-commit', 'sandcastle/issue-987654');
  try {
    state.phase = 'fix';
    state.sessionId = 'session-id';
    state.findings = ['[spec] note: fix this'];
    saveCheckpoint(state);
    assert.equal(checkpointExists(ticket.number), true);
    assert.deepEqual(loadCheckpoint(ticket.number), state);
  } finally { clearCheckpoint(ticket.number); }
  assert.equal(checkpointExists(ticket.number), false);
});
