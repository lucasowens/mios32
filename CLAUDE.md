# CLAUDE.md — fork working notes

This is a personal fork of MIDIbox SEQ V4 being developed into a live generative-
musical instrument. One-man project, no timelines, iterative.

## Where the design lives

- **`doc/MBSEQV4_GENERATIVE_PLATFORM_DESIGN.md`** —
  the durable design home (at repo-root `doc/`, NOT under the app dir).
  **Part I** is the committed spine; **Part II** is design-ahead reference that is
  *provisional and not committed* (build only after the §8 first-build GO/NO-GO
  proves the core by ear). Read §9 (decisions) and §10 (open questions) first when
  picking up cold.
- `apps/sequencers/midibox_seq_v4/doc/MBSEQV4_REFERENCE.md` — derived facts about
  the existing codebase (bus model, versions, TODOs). Owns codebase facts; the
  design doc owns the model.
- `apps/sequencers/midibox_seq_v4/doc/MBSEQV4_MANUAL_FORK.md` — user manual for
  shipped fork features.

## Working discipline (from the design doc §2)

- POC code is disposable; nothing must stay backward-compatible.
- Prove musical ideas **live, by ear, before** building the infrastructure that makes
  them performant. The §8 build order is music-first on purpose — do not reorder it
  to put infrastructure (the render-cache) first.
- Constraints are *materials*, not guardrails: every processor dial must sweep 0→max
  including a true pass-through at 0 (bipolar dials: pass-through at the center
  detent).
- Build toward a sound you can hear, not the next piece of capability. When in doubt,
  build less and listen sooner.
- The unit of by-ear validation is a **workflow bundle** — the smallest *playable
  loop* (set up fast, sweep, release, capture, return), not the smallest feature.
  GO/NO-GO gates sit at the workflow level; infrastructure is licensed when it's on
  a bundle's critical path (design doc §2.7).
- New musical transforms are **born as render-stack processors**, never emission-time
  effects (emission effects are invisible to `OutputActive` and force bake code at
  bounce — design doc §3).

## Plan storage convention

Multi-session design plans live in `doc/plans/YYYY-MM-DD-<slug>.md`, git-tracked.
One-shot session plans stay in `~/.claude/plans/`. Plans are scaffolding — archive or
delete them once executed into the design doc / code. The design doc is the durable
home; plans are the workflow that gets there.

## Before trusting any budget or platform claim

Line numbers drift; symbol/function names are the durable anchors. Verify
platform-internals claims against the actual source in this fork (not mainline, not
memory) before building on them — the bus model, drum-note path, and RAM sizing have
all needed correction against source at least once.
