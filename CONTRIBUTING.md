# Contributing to Inkstow

For build, test, and formatting commands, see `README.md` and `AGENTS.md`. The conventions below apply when changing C++ code; for frontend code, follow the patterns in the surrounding TypeScript, HTML, and CSS.

## Code style

### Match existing style

Before editing a file, read nearby code and identify its whitespace and grouping patterns. Make new code look like it belongs alongside the surrounding implementation rather than applying generic C++ conventions. Avoid reformatting unrelated code; use the repository's `.clang-format` for changed C++ files.

### Whitespace and layout

- Separate logically distinct operations with blank lines.
- Keep tightly related statements together without blank lines.
- Do not add blank lines just to make functions look spacious.
- Prefer short, visually cohesive blocks, matching the surrounding spacing and grouping.

### Control flow

Prefer early returns to deeply nested conditionals.

### Casts

Use functional casts such as `int(x)` and `double(x)` for primitive conversions. Reserve `static_cast` for pointer and reference conversions.
