# PR draft for issue #{{ISSUE}}

{{ISSUE_JSON}}

Host-captured verified diff:
```diff
{{DIFF}}
```

Implementer summary: {{SUMMARY}}
Review notes: {{NON_BLOCKING}}
Out-of-scope observations: {{NOTICED}}

Use the `pr` skill's format and evidence sections yourself; do not spawn agents. You have read-only file access; do not edit files, commit, build, push or open the PR. Cite only verification reported above as done by the host, not invented test output. The host appends `Closes #{{ISSUE}}`. Write a concise conventional-commit title and markdown body. Finish with:
<pr>
{"title":"...","body":"..."}
</pr>
