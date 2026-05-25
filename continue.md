# Session Continuation — input-bus MON slice COMPLETE; awaiting operator eyes-on

## ▶ 0. Read these first (90 seconds)

1. **The 14-task input-bus MON slice is fully landed** (V9 §7.2 — MON is per-input-side-node: input channels, aux buses, and FX returns all carry the toggle). Engine + persistence + UI plumbing all green. **The operator has not yet eyes-on'd the slice** — that's T14 step 3, the recipe below in §1.
2. **Branch tip is `ceb3296`** on `master` (local == origin) before this handoff refresh; the handoff refresh you're reading lands as the next commit.
3. **Clean Release build is on disk** at `build/app/IDA_artefacts/Release/IDA.app` (the Desktop alias `IDA` points at it). Launch via the alias and run the recipe.

---

## ▶ 1. NEXT STEP — operator eyes-on per the T14 recipe

```
1. Launch IDA via the Desktop alias.
2. On the Input Mixer, add an aux bus (blank-area gesture).
3. Click the new bus strip's MON button — label flips Off → MON; tooltip
   updates; a new strip appears in the Output Mixer's MON band, labelled
   with the bus's name.
4. Play audio into a channel that routes into the bus; confirm the bus's
   summed signal is audible.
5. Click MON again — label flips MON → Off; the strip vanishes from the
   MON band.
6. Set MON On, save the session, quit, relaunch, load the session.
   Confirm: MON button shows MON, the MON-band strip is back, the audio
   is audible.
7. Repeat 2-3 on an FX return — same behaviour.
```

If anything in steps 1-7 doesn't behave as described, capture the specific
step + failure mode and file a follow-on slice. Otherwise the slice is
ready to consider closed.

---

## ▶ 2. What landed this chat (4 new commits + 1 follow-on, plus prior session's 13)

This chat (T11-T13 + follow-on, oldest first):

| SHA | Subject |
|---|---|
| `533537a` | feat: refreshOutputMixer{,MonChannels,Destinations} walk bus MONs alongside channel MONs (V9 §7.2) |
| `7ddb27a` | fix: bus MON rows — resolveMonChannelId + MON probes branch on bus-vs-channel; refresh MON band on bus rename |
| `f6909e6` | feat: chooseFileAndLoad — bus MON replay block mirrors channel MON replay (V9 §7.2) |
| `ceb3296` | feat: refreshInputMixer + rebuildBusStrips push bus MON button states from engine (V9 §7.2) |

Prior session's slice commits (T1-T10 + design + plan, kept for reference):
`b911f03`, `2cd00d9`, `8c8c99a`, `b13577a`, `6196cd2`, `611a437`,
`7bdabca`, `86a0ad8`, `a237339`, `afaccbe`, `ef066fd`, `f7ad3b5`,
plus `2da5459`, `8d34fc0`, `9bd87ea` earlier still.

Notable on `7ddb27a`: code-quality review of T11 caught that the prior
commit wired bus MON rows into the Output Mixer pane but the existing
MON callbacks (gain, mute, pan, width, send, destination, EQ/CMP probes,
insert chain) all still resolved via `channelMonitorOutputChannel` —
silently no-op'ing on bus-sourced rows. The fix is a one-spot dispatch
in `resolveMonChannelId` (which feeds every operator gesture) plus the
same dispatch in the three MON probe collectors, plus a missing
`refreshOutputMixerMonChannels()` call in `onBusRename` so a rename
propagates to the MON-band strip label. Without this commit T14's
recipe steps 3-6 would all fail.

---

## ▶ 3. Baseline as of `ceb3296`

| Check | Result |
|---|---|
| Branch | `master`, local == origin |
| Clean Release build (`rm -rf build && cmake -B build … && cmake --build build --target IDA`) | clean (only the standing ld duplicate-libs warning) |
| `ctest -E "(PluginEditor|MainComponentPlug)"` | **726 / 726 passed** |
| Operator GUI verify | **pending — recipe above** |

The one continued exclusion (`PluginEditor|MainComponentPlug`) reflects
the standing skip of the `MainComponentPluginEditorTests` SIGTERMs, which
are pre-existing and not in scope for this slice (see §4).

---

## ▶ 4. Out of scope (queued; demoted)

1. **Phrase strip meter is dead** (carried over).
2. **`MainComponentPluginEditorTests` SIGTERMs** — pre-existing, six+ continue.md's now.
3. **`InputMixerMonitorMuteLeakTests.cpp` references the deleted `DirectLayer` header.** Compile-fails; build skips.
4. **TAPECOLOR OTTO inbox** — `[FROM OTTO → IDA]` `needs-ack` entries for Phases 6/7/8. Operator standing direction: defer while OTTO is debugging.

---

## ▶ 5. House rules respected

- Worked on `master`, no feature branch.
- Commit + push to `origin/master` after every task (and after the
  T11 follow-on fix).
- Single-line commit titles.
- Subagent-driven implementation for T11 with spec + code-quality review;
  reviewer findings drove the `7ddb27a` follow-on. T12 + T13 were direct
  in-chat edits (trivial mirrors of channel-side patterns) and built +
  tested green before commit. T14 was clean-rebuild + recipe.
- No `--amend` anywhere; the one follow-on landed as a new commit.
- Final clean `rm -rf build` + Release reconfigure + `cmake --build build`
  before flipping to operator-verify, per project `CLAUDE.md`.

---

## ▶ 6. Resume protocol for next chat

If the operator confirms the slice works:
- This slice closes. Open `docs/superpowers/plans/2026-05-25-input-bus-mon.md`
  and tick the Task 11-14 boxes (the plan tracks checkbox state per step).
- The next near-term order per project standing direction: Output Mixer
  feature parity work, then transport + remaining metering. Read
  `docs/IDA_Whitepaper_V9.md` + `docs/design/mixer-design.md` for the
  next slice scoping.

If the operator finds a regression in the recipe:
- Capture exact step + observed vs. expected, then debug from the engine
  → persistence → UI plumbing layer that's implicated. T11's follow-on
  is the most-recent precedent — most likely culprits are still around
  `resolveMonChannelId` (for gesture failures) or `chooseFileAndLoad`'s
  replay block (for load-time regressions).

---

*End of input-bus-MON 14/14 handoff. Operator eyes-on is the gate.*
