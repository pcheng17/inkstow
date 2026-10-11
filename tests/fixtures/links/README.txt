Disposable fixture for issue #8. Obsidian 1.14.4 bundled app.js static inspection (docs/obsidian-appimage-inspection.md priority 2) suggests the targets below; these are NOT observed Obsidian navigation. No live verification was possible: no DISPLAY, WAYLAND_DISPLAY or Xvfb. Obsidian executable was not run; running app version, settings and loaded bundle were not verified. The core and offscreen UI tests verify Inkstow behavior only.

From A/Source.md, static hypotheses / Inkstow test expectations:
[[Twin]] -> A/Twin.md (duplicate names in A/, B/, root)
[[B/Twin]] -> B/Twin.md (exact vault-root path)
[[b/twin]] -> B/Twin.md (case-insensitive full-path match; hypothetical)
[[Wrong/Twin]] -> unresolved (no loose basename fallback for qualified path; hypothetical)
[[/B/Twin]] -> B/Twin.md (leading slash exact root path)
[[/Twin]] -> Twin.md (leading slash root file)
[[tWiN]] -> A/Twin.md (case-insensitive basename fallback)
[[Twin.md]] -> A/Twin.md (explicit extension)
[[Twin#Section|Display]] -> A/Twin.md (actual # Section heading exists; fragment/alias stripped for file lookup; heading navigation not implemented)
[[photo.png]] -> assets/photo.png (attachment lookup; Inkstow attempts to open it externally after safety validation)
[[Absent]] -> unresolved
Additional tests: [[LOWER]] -> Sub/lower.md; [[/b/twin]] -> B/Twin.md (hypothetical); [[/a/twin]] -> A/Twin.md (hypothetical); [[/Absent]], [[../Twin]] unresolved. Exact case, duplicate tie breaks, path ordering, heading navigation and attachment opening require live Obsidian observation on a throwaway vault before claiming compatibility.
