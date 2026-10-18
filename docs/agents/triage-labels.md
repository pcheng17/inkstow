# Triage and agent labels

General triage labels remain `needs-triage`, `needs-info`, `ready-for-human`, and `wontfix`. Sandcastle uses **exactly one** of these lifecycle labels on an issue:

| Label | Meaning |
| --- | --- |
| `agent:ready` | Approved for a fresh autonomous run; no open native blockers. |
| `agent:running` | Sandcastle has claimed the issue and is working or recovering it. |
| `agent:blocked` | Run paused for a human decision, dependency, verification, or infrastructure repair. |
| `agent:review` | PR opened and ready for review. |

To start a new issue, add `agent:ready` and run `npm run sandcastle -- <number>`. For a paused run, inspect the issue handoff, `.sandcastle/state/issue-<number>.json`, and the saved branch/logs. Once the blocker is resolved, run `npm run sandcastle -- --resume <number>` while the issue has `agent:blocked` or `agent:running`. This reuses the saved base, branch, and captured Pi session when available. A run with no checkpoint must be re-triaged with `agent:ready` instead. Never delete the checkpoint or branch just to bypass a stop without inspecting them.

**Deployment migration (after the orchestrator PR merges):** Create the four new repository labels, move issues tagged `ready-for-agent` to `agent:ready` (or `agent:blocked` if they have open blockers), and then delete the old label. Migrate before launching the new orchestrator. Existing `ready-for-agent` is intentionally not read by the new code. Leave current GitHub labels unchanged until the PR merges so the deployed orchestrator continues to work.
