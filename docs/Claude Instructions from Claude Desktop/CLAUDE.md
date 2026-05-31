# CLAUDE.md

<!--
This file loads automatically at the start of every Claude Code session.
If you already have a CLAUDE.md, paste the "IDA Build Protocol" section
below into your existing file rather than replacing it.
-->

## IDA Build Protocol — read this every session

You are implementing IDA across many short sessions. Context degrades past
~35%, so we keep each session small and push all durable state to disk. The
rule that governs everything: **context is volatile, disk is durable.** Any
fact that must survive to the next session lives in `PROGRESS.md`, not in
your head.

### Session start (always do this first, before anything else)
1. Read `PROGRESS.md` in full.
2. Read ONLY the section of `PLAN.md` for the next unchecked task. Do not
   read the whole plan.
3. State the task ID you're about to do and its Definition of Done, in one
   or two lines. Then proceed — wait for nothing.

### While working
- Do exactly ONE task per session. Do not start a second task, even if the
  first finishes quickly and context still looks light.
- Read narrowly. Use `grep`, `head`, and specific line ranges instead of
  reading whole files. Never re-read a file already in context.
- Make the smallest change that satisfies the task. Do not refactor
  adjacent code or "improve" things that aren't in the task scope.
- When you discover a decision worth keeping or a gotcha that cost you time,
  hold it for the end-of-session log entry. Do not rely on it carrying over.

### Session end (when Definition of Done is met, OR context feels heavy —
### whichever comes first)
1. Run the task's Verify command. Paste the actual result.
2. Append an entry to `PROGRESS.md`. **Append only — never overwrite or
   rewrite earlier entries.**
3. Check the task's box in `PLAN.md` (`- [ ]` becomes `- [x]`).
4. STOP. Tell me the session is complete and which task is next. Do not
   begin the next task.

### Authority to stop early
You are explicitly authorized — and expected — to end a session at a clean
boundary rather than push through a degrading context window. Stopping early
with a clean log is a success, not a failure. If you cannot finish a task
before context gets heavy, log what you did, note the exact remaining work
under "Next" in `PROGRESS.md`, leave the box unchecked, and stop.

### Hard rules
- Never overwrite `PROGRESS.md`. Append only.
- Never tick a `PLAN.md` box whose Verify command has not actually passed.
- Prefer ending the session over `/compact` at a task boundary. A fresh
  session that reads `PROGRESS.md` reconstructs exactly what we chose to
  persist; `/compact` summarizes lossily and can drop the detail we needed.
