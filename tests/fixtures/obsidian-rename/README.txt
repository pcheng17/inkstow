Manual Obsidian compatibility fixtures (not automated test assertions).
See docs/obsidian-runtime-observations.md and .agents/skills/obsidian-behavior/SKILL.md.

Copy ONE of simple/, unique/ or duplicate/ to a new temporary vault for EACH trial.
Use Obsidian's own rename/move action and record the prompt and the resulting
Markdown bytes. Never operate on the checked-in fixture.

simple/: Source.md says "See [[Target]]." and Target.md has a short heading
  and body. Try renaming Target.md to Renamed.md, choosing Just once or Do
  not update in independent trials.

unique/: one target in subdir/, referenced as [[Target]] and [[subdir/Target]].
  Try renaming the file to NewTarget.md, or renaming subdir/ to renamed/.

duplicate/: A/Target.md and B/Target.md have distinct headings; C/ is an
  otherwise-empty destination folder (.keep keeps it in git and is ignored by
  Obsidian's file explorer). RootSource.md, A/Source.md, and B/Source.md all
  have [[Target]] links. First follow each link to establish its destination.
  Try renaming A/Target.md to A/NewTarget.md or moving A/Target.md to C/.
