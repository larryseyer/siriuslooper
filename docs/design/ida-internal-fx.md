# IDA Internal FX — Design

Single source of truth for IDA's built-in effects. The whitepaper
(`docs/IDA_Whitepaper_V8.md` §6.6) names the four built-in FX as
core product; this doc enumerates their parameter surfaces and the union
slot type that lets them share insert chains with third-party VST/CLAP
plugins. Implementation details (adapter wrapping, audio-thread dispatch,
config-swap semantics, sequencing) live in
`docs/superpowers/specs/2026-05-22-otto-integration-design.md` Decision 3;
this doc is the product-level reference and links there rather than
duplicating.

---

## The four built-in FX

The four effects ship with every copy of IDA. They are not staging for a
future tier and they are not a "preset bundle" — they are core product, and
their DSP comes from OTTO's header-only Player FX (single source of truth in
`external/OTTO/src/otto-core/include/otto/effects/`, dependency-only on
`juce_dsp`). Each is wrapped by a IDA-side adapter that lives in the
insert-chain host.

| Built-in FX | Purpose | DSP source |
|---|---|---|
| **EQ** | Five-band tone shaping (HPF + low shelf + mid parametric + high shelf + LPF). The default surface for surgical and broad tone work alike. | `PlayerEQ.h` |
| **Compressor** | Single-band dynamics with parallel mix and sidechain HPF. Covers gentle bus glue through aggressive limiting. | `PlayerCompressor.h` |
| **Reverb** | Convolution reverb with preset IR selection, pre-delay, decay shaping, time stretch, damping, and a baked-in 3-band EQ. Sources its impulse responses from the bundled OTTO asset tree (see `project_otto_assets_out_of_git`). | `PlayerIRConvolution.h` |
| **Delay** | Stereo delay with tempo-sync (1/32 through 1/1, including triplet and dotted), free-running ms mode, feedback with low-cut / high-cut filtering, and an optional ping-pong mode. | `PlayerDelay.h` |

The four are deliberately the minimum set that covers the foundations of mix
processing (EQ + dynamics + ambience + time) without any third-party install.
Third-party VST/CLAP hosting is **additional** to this set, not a substitute
for it — it lets the performer reach plugins they already own and like, but
opening a fresh session with no plugins installed still gives them a complete
mixer.

---

## Parameter surface

All four FX share one canonical configuration struct,
`otto::effects::PlayerEffectsConfig`, defined at
`external/OTTO/src/otto-core/include/otto/effects/PlayerEffects.h`. That
struct is the source of truth for the parameter ranges and defaults below.
IDA's adapter for each FX reads the relevant subset of the struct and
exposes it on the strip detail panel.

### EQ — 5-band

- `eqEnabled` (bool, default `false`).
- High-pass: `eqHPFreq` (20–500 Hz; 20 = bypass), `eqHPSlope` (6 / 12 / 18 / 24 dB/oct).
- Low shelf: `eqLowGain` (±12 dB), `eqLowFreq` (40–500 Hz), `eqLowQ` (0.1–10).
- Mid parametric: `eqMidGain` (±12 dB), `eqMidFreq` (200–8000 Hz), `eqMidQ` (0.1–10).
- High shelf: `eqHighGain` (±12 dB), `eqHighFreq` (2000–16000 Hz), `eqHighQ` (0.1–10).
- Low-pass: `eqLPFreq` (2000–20000 Hz; 20000 = bypass), `eqLPSlope` (6 / 12 / 18 / 24 dB/oct).

### Compressor

- `compEnabled` (bool, default `false`).
- `compThreshold` (−60 to 0 dB), `compRatio` (1:1 to 20:1), `compAttack`
  (0.1–100 ms), `compRelease` (10–1000 ms), `compMakeup` (0–24 dB).
- `compMix` (0–1; parallel-compression dry/wet).
- `compSidechainHPF` (bool, default `true`; keeps low end from triggering
  the detector).

### Reverb (convolution)

- `irEnabled` (bool, default `false`).
- `irPresetName` (string; resolves to a file under
  `${OTTO_ASSETS_DIR}/IR/<preset>/...` — see
  `project_otto_assets_out_of_git`).
- `irPreDelay` (0–100 ms).
- Envelope shaping (pre-baked into the IR at load time):
  `irDecay` (0–100 % of IR length), `irDecayCurve` (±100; negative = slower
  tail, positive = faster), `irTimeStretch` (50–200 %), `irDamping`
  (0–100 % HF absorption).
- Baked 3-band EQ: `irEqEnabled`, `irEqLowFreq` (50–500 Hz),
  `irEqLowGain` (±12 dB), `irEqMidFreq` (200–8000 Hz),
  `irEqMidGain` (±12 dB), `irEqMidQ` (0.5–8), `irEqHighFreq`
  (2000–12000 Hz), `irEqHighGain` (±12 dB).
- `irMakeupGain` (±24 dB).

### Delay

- `delayEnabled` (bool, default `false`).
- `delayMix` (0–1 dry/wet).
- Time: `delaySyncEnabled` (bool) chooses between
  `delaySyncValue` (musical: 1/32 through 1/1, including triplet and
  dotted) and `delayTimeMs` (free-running, 1–2000 ms).
- `delayFeedback` (0–0.95).
- Feedback filtering: `delayHighCut` (1000–20000 Hz) and
  `delayLowCut` (20–500 Hz) shape repeats.
- `delayPingPong` (bool; left feeds right, right feeds left).
- `delayEqEnabled` (bool; return-channel EQ bypass).

When any parameter range or default below changes in OTTO's
`PlayerEffectsConfig`, this doc gets updated in the same change. The struct
is the source of truth; this doc is the readable mirror.

---

## Union slot type contract

Every node in the routing graph (channel, bus, FX return) carries an insert
chain of up to **eight slots**. Each slot is one `EffectChainEntry`:

```
EffectChainEntry {
    kind   : SlotKind = Empty | Internal | Plugin
    bypass : bool
    // exactly one of the following is meaningful, per kind:
    internal : InternalFXDescriptor   // when kind == Internal
    plugin   : PluginDescriptor       // when kind == Plugin
}
```

- **`Empty`** — slot is unallocated; the host skips it at render time.
- **`Internal`** — slot identifies one of the four built-in FX by an
  `InternalFxId` (`EQ` / `CMP` / `RVB` / `DLY`) and carries the
  parameter payload (typically a copy-on-write reference to a
  `PlayerEffectsConfig` sub-block). The host owns a per-slot adapter that
  wraps the OTTO Player FX (see Decision 3 link below).
- **`Plugin`** — slot carries a `PluginDescriptor` (format + identifier +
  state blob) for an externally-hosted VST/CLAP. The existing
  out-of-process plugin host (M7) handles dispatch. This shape is
  unchanged from the form `EffectChain` already ships.

Both kinds share the same chain ordering, bypass behaviour, persistence
shape (`SessionFormat` round-trips the discriminant; pre-union sessions
forward-load as `Plugin`), and 8-slot cap. The chain itself remains the
copy-on-write data model `EffectChain` already implements — no runtime
processor objects live on the chain, only descriptors. Runtime processors
are owned by the host and indexed by `(busId, slotIdx)`.

The contract lands in **Phase 4 / T2** of the P7 umbrella plan (engine
data model + persistence). The **adapter implementations** that make the
`Internal` case actually instantiate one of OTTO's header-only Player FX
land separately in **T3** (one sub-task per FX, sequenced
EQ → CMP → DLY → RVB).

---

## Adapter architecture

The full adapter blueprint — variant slot, host-owned adapter storage keyed
by `(busId, slotIdx)`, config-swap parameter pattern (UI scratch →
release-store commit → audio-thread acquire-load), RT-safety alignment,
sequencing — lives in
`docs/superpowers/specs/2026-05-22-otto-integration-design.md` Decision 3.
That spec is the source of truth for the implementation; this doc does not
restate it.

---

## Operator-UI reachability rule (2026-05-24)

EQ and CMP are first-class on every channel / bus / FX-return: they live
on the strip's dedicated **EQ** and **CMP** tabs, not in the insert
chain. The insert picker therefore offers only **DLY**, **RVB**, and
(once the host plugin scanner is healthy) **3rd-party VST/CLAP**. EQ
and CMP do not appear as choices in the insert picker; double-inserting
them would be a redundancy with no audible upside, since the strip
already runs an EQ and a CMP in front of the insert chain by design.

The data model is unchanged — `EffectChainEntry` with
`InternalFxId::kEq` or `kCmp` remains a legitimate construction by
non-operator code paths (render pipeline, programmatic chain setup). The
rule is strictly about the operator-facing **picker** UI.

Enforcement lives in `ui/src/InsertChainPopup.cpp`: the picker's
ComboBox omits EQ and CMP, and `handlePickerChange` defensively rejects
those ids so the policy is centralized in one place. The contract is
pinned by `tests/InsertChainPopupTests.cpp` ("InsertChainPopup picker
rejects EQ and CMP").

---

## What this doc does NOT cover

- **Implementation specifics** of the adapter wrapping. See Decision 3 in
  `docs/superpowers/specs/2026-05-22-otto-integration-design.md`.
- **Third-party plugin scanner repair.** The host plugin scanner currently
  GUI-locks on Scan at every entry point (see `project_plugin_scanner_broken`
  and Bug 2 in `todo.md`). Its own slice ("P7-scanner") is gated on that fix;
  until it lands, the insert UI ships internal-FX-only with a greyed-out
  "VST/CLAP plugin… (scanner unavailable)" entry so the absence is visible.
- **Per-FX automation and Mix Scene morph.** Mix snapshots and the
  energy-arrangement morph engine (Output Mixer only) are the
  Energy Arrangement design, parked at/after P8 — see
  `~/.claude/plans/this-will-be-a-merry-rabin.md` and whitepaper §6.8.
