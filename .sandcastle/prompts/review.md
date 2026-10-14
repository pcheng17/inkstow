# {{AXIS}} review for issue #{{ISSUE}}

Spec, including parent and comments:
{{ISSUE_JSON}}

Fixed point: {{BASE_SHA}}. Commits:
{{COMMITS}}

Host-captured diff (three-dot from fixed point):
```diff
{{DIFF}}
```

Read-only review: inspect files using the `read` tool; do not edit, commit, build, call subprocesses, or spawn agents. Work alone. The host has already verified the build and tests. For standards, read `AGENTS.md`, `README.md`, and step 3 of the `code-review` skill (standards sources, smell baseline and lowered-bar check); apply those rules directly. For spec, compare every issue requirement with the diff and surrounding code. Review ONLY the {{AXIS}} axis; classify hard violations, broken behavior, missing requirements, scope creep, and lowered-bar moves as blocking; judgement calls and nits as non-blocking. Every finding must specify a file and concrete remedy.

Finish with exactly one block (empty arrays allowed):
<review>
{"blocking":[{"axis":"{{AXIS}}","file":"path","finding":"problem and remedy"}],"nonBlocking":[]}
</review>
