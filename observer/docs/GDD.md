# THE OBSERVER — Game Design Document

*Case One of THE CATALOGUE · Crit Hit Studio · built on the IRIS engine*

> The player should finish THE OBSERVER asking:
> **"Did I discover the monster, or did I create it?"**

## 1. Premise

You are the temporary night caretaker of **Halcyon Court**, a 1961 apartment
block emptied for demolition. The job is mundane: watch cameras, log leaks,
reset breakers. The building begins behaving incorrectly, and the game's one
law takes over:

**Observing an anomaly makes it more real.**

## 2. Pillars

1. Psychological horror over jump-scare economy.
2. Observation is the core verb *and* the core resource drain.
3. Adaptive, authored horror: the game studies how you're afraid and answers.
4. Unreliable information: CCTV, photographs, mirrors, your own logbook.
5. One entity. No bestiary. The Witness is enough.
6. Impossible architecture, delivered honestly (no fake walls — real remaps).
7. No sanity meter, ever. The frame itself carries the dread.
8. Deliberate ambiguity. The game never confirms what happened.

## 3. The core loop

**Notice → Doubt → Investigate → Observe or Ignore → Consequence → Doubt again**

- Work orders (the clipboard, `Tab`) pull the player through the building.
- The director schedules anomalies along the route and off it.
- Every anomaly can be stared at, photographed, ignored, or fled.
- Attention is silently banked (`manifest` per anomaly, global `attention`),
  and returns with interest.

## 4. Systems

### 4.1 Observation

Every active anomaly is an *Observable* with position and radius.
Per frame, gaze is scored:

```
focus  = smoothstep(cos 25°, cos 2.5°, dot(camFwd, dirTo))   // center-weighted
gain   = focus * distFalloff * dwellRamp * dt                // dwell builds up
manifest += gain;  attention += gain * 0.12
```

Line-of-sight is required (walls and closed doors block). The viewfinder
multiplies gain ×2.1 — the camera is a better eye, which is the trap.
Photographs spike `manifest` +2.2 on every subject in frame and are the only
way to see *photo-revealed* apparitions.

### 4.2 The hidden paranoia profile

Never surfaced, never named in-game. Nine channels:

| Channel | Fed by | Answered with |
|---|---|---|
| BEHIND | fast 130°+ turns (checking your back) | sounds and figures behind you |
| MIRROR | seconds spent facing mirrors | reflection-only divergences |
| CCTV | monitor time per camera | figures on the feeds you watch |
| DOOR | re-checking doors, rattling locked ones | slams, doors ajar again |
| LIGHT | switch flips, flashlight toggling | flicker, room blackouts |
| PHOTO | exposures taken | photo-only apparitions |
| SOUND | (ambient baseline) | knocks, pipes, crying, footsteps above |
| SPACE | corridor time | prop drift, room swaps, stair loops |
| WITNESS | gazing at it | more of it |

`weight(c) = clamp(score[c] / Σscore * K)` biases the director's roulette:
`w = base * (0.55 + 2.2 * weight(channel))`. Scores decay slowly, so the
building's answer tracks the player's *current* habits.

### 4.3 The director

Data-driven templates (`content/anomalies.json`): channel, act window,
cooldown, once-flag, optional `unobserved` requirement (the target room must
not be in the visible set — **change happens where you aren't looking**).
Pick cadence follows the act (`interval`), accelerating with global
attention: the more real you've made it, the faster it comes.

### 4.4 The Witness

Stages, driven by cumulative observation (`knowledge`, photographs ×1.6):

| Stage | Threshold | Presentation | Behavior |
|---|---|---|---|
| 0 absent | — | — | — |
| 1 silhouette | 2.5 | featureless, wrong proportions | distant; vanishes if approached < 5 m or stared at > ~4 s |
| 2 forming | 11 | unfinished clay + static | holds ground longer; whispers on vanish |
| 3 mimic | 26 | almost a person, your uniform | *moves only while unobserved*, approaches; appears in mirrors and on CCTV |
| 4 copy | 46 | you | scripted finale only |

Acts cap the stage (1:0, 2:1, 3:3, 4:3, 5:4) so authored pacing always wins.
Contact never kills: a blackout relocation and one identity fragment lost
(`touched` flag — it flavors the ending). Death screens reset fear;
displacement compounds it.

### 4.5 Impossible space

- **Threshold doors** — stairs and the vault entry pass through darkness;
  the destination is a door id, and *redirects* can quietly rewrite it
  (stairs that return you to the floor you left).
- **Room swaps** — prop sets `x@A` / `x@B` flip while unobserved (4C's
  furniture has two truths).
- **The elevator** grows a button (`-1`) in act 4.
- **The exit** in act 5 opens back into the lobby. Three attempts teach the
  player the rule the building has been teaching all night: it is kept
  *for*, not *in*.

### 4.6 Photography

`C` raises the viewfinder (44° FOV, scanlines, REC chrome), `LMB` exposes:
flash-white frame, capture of the scene buffer, instant-photo processing
(desaturate, lift, vignette), saved as a real PNG in the user directory and
into the in-game gallery. Photo-reveal props render **only during the
capture frame** — the print shows what the room didn't.

### 4.7 CCTV

Eight cameras. The security monitor (interact) turns the main view into the
selected feed with mono-green post, timestamp, and camera cycling (`Q/E`,
`1–8`). Watching feeds is observation: it counts, it teaches, and at high
stages the cameras start showing *you* in rooms you aren't in.

### 4.8 Mirrors

Real reflection render passes (portal-fitted cameras onto render targets).
Reflection-only props and the `mirror_witness` flag put one more person in
the reflection than the room holds. The dancer in 2C covered the tall one
for a reason.

## 5. Structure — one night, five acts

| Act | Card | Cap | Content |
|---|---|---|---|
| I | 23:02 — HANDOVER | 0 | Mundane rounds, controls, three tasks; the mug moves when unobserved |
| II | 00:31 — DISCREPANCIES | 1 | Breaker, knocking on 2, the covered mirror, first distant sighting, CCTV figure |
| III | 02:14 — THE WITNESS | 3 | Crying in 3B, the darkroom prints a photo of you asleep, the dictaphone, records: 1961 |
| IV | 03:47 — UNFOUNDED | 3 | 4A unsealed, 4C rearranges, stairs loop, button −1, Site 3, the vault |
| V | 05:12 — COPY | 4 | The exit refuses; descent; *"I only know what you showed me."*; morning; **two of you leave on CAM 1** |

~18 meaningful rooms: lobby, security, mail, laundry, service corridor,
elevator(×6), halls 1–4, 1B+bath, 1A, 2A, 2C, 3B, 3D (living/bed/darkroom),
4A, 4C, basement, records, sub-corridor, the vault.

## 6. Endings

All endings share the two-figures CCTV shot. The hidden profile picks the
post-credits line (never labeled):

- **witnessed** (default) — "It knows the way now."
- **documented** (≥5 Witness photographs) — "The photographs remain. All of them are recent."
- **reflected** (mirror gaze > 75 s) — "Your reflection still checks behind itself."
- **replaced** (touched) — "It was let out. Something was."

The game never confirms which one walked out first.

## 7. Presentation

- 71° FOV first person, head-bob, no visible body.
- Film grain (luminance-weighted), vignette, chromatic aberration, fear-warp
  breathing at high dread, full CCTV mode. All post, no HUD meters.
- Subtitles on by default; work orders diegetic on the clipboard.
- Audio: procedurally synthesized foley bench + generated voice.
  The Witness's line is the protagonist's TTS voice, detuned and layered —
  it has your voice because you gave it away one syllable at a time.

## 8. Controls

WASD move · mouse look · Shift run · Ctrl crouch · E interact · F flashlight
· C camera · LMB expose · Tab clipboard · Q/E cycle feeds (monitor) ·
F5/F9 quicksave/load · Esc pause.

## 9. Scope & tech

Single-player, 2.5–4 h first playthrough (act-clock paced), one building,
one antagonist. Custom **IRIS** engine: Vulkan 1.3, dynamic rendering,
bindless-lite textures, CPU-testable headless mode (lavapipe CI),
GLFW window, miniaudio, stb, ~9 kLOC. Content is 100% data-driven JSON —
the whole night is moddable by editing `content/`.
