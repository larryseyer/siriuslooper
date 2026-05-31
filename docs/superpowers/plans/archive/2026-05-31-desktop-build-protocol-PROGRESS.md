# IDA Progress Log (append-only)

<!--
This is the project's real memory across sessions. APPEND new entries at the
bottom. NEVER overwrite or rewrite earlier entries — losing history defeats
the purpose of this file.

Track decisions and gotchas, not just status. Status tells the next session
what's left; decisions tell it WHY things are the way they are so it doesn't
undo them. Decisions and gotchas are the expensive things to reconstruct
after a context reset — capture them here.

Entry format (copy this block for each session):

## YYYY-MM-DD — <Task ID> <done | partial | blocked>
- What: <one line on what changed>
- Verify result: <PASS/FAIL + the actual command output, e.g. "PASS ctest 4/4">
- Decisions: <choices future sessions must respect, and why>
- Gotchas: <traps that cost time, so the next session avoids them>
- Next: <the exact next task ID, or remaining work if this was partial>
-->

## 2026-05-31 — Setup
- What: Created CLAUDE.md (IDA Build Protocol), PLAN.md (task plan), and this
  log. No code changed yet.
- Verify result: N/A
- Decisions: One task per session; disk is the source of truth; sessions end
  at clean boundaries before context degrades (~35%).
- Gotchas: none yet.
- Next: First coding/decomposition session — see the first unchecked task in
  PLAN.md.
