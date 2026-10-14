# Issue #{{ISSUE}}

{{ISSUE_JSON}}

You are in a sandbox worktree on `{{BRANCH}}`, based on `{{BASE_SHA}}`. The host owns issue updates, pushing and PR creation. Work only on this branch. The issue and its parent/comments above are the spec; the sandbox has no GitHub credentials. Follow `AGENTS.md` and the relevant README sections. Use disposable fixtures, not a personal vault.

1. This host-orchestrated workflow is an exception to Harness Kit's global preference for subagents: work alone and do not spawn agents. Read step 3 (standards and lowered-bar check) of the `code-review` skill before editing. Do the review yourself later.
2. Load `tdd` and follow its test-first loop. Use `diagnosing-bugs` for difficult failures and `codebase-design` or `dod` when their subject applies. If a decision needs a human, report blocked rather than guessing.
3. Implement only the issue. Track unrelated observations under `noticed` without editing them. Commit each green slice with only that slice staged. Never bypass hooks. After each slice build and test the changed behavior; before finishing, check `git status --porcelain` is empty.
4. The host runs WebView-enabled build, all CTest, formatting and C++ clang-tidy before review. Do not claim a failed check passed.

Finish with one JSON block:
<implementation>
{"status":"done","summary":"what changed and why","noticed":[]}
</implementation>
If blocked, use `"status":"blocked"` with the decision needed in `summary`, and leave `noticed` empty. Stop without committing broken or half-finished changes.
