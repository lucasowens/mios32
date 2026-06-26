"""play-readiness #5 — render change-detection (sweep/quiet by live-input signature).

THE BUG (measured on hardware): chord_mask / tension / live-pitch slots used to force a FULL
per-track re-render EVERY tick — memcpy(par)+memcpy(trg) + a processor sweep — even when the
field was perfectly STATIC. The render runs in the +4 emission task's tick prologue, so with all
16 tracks gripped under a GRAVITY field it pegged the CPU (~95% render duty on full-buffer
tracks) and starved the lowest-priority +2 UI task: the control surface (LEDs/LCD/buttons) went
dark while audio limped. The user's real patterns locked up at ~6 gripped tracks.

THE FIX (SEQ_CORE_RenderTracks): each tick, fold every LIVE input a force-dirty processor reads
(the held chord on a bus, the global GRAVITY dial, the transposer note, the scale/root) into a
per-track u32 signature; re-render a track only when its signature CHANGES. A static field is
signature-stable → zero renders → UI fully alive even with all 16 gripped. A dial sweep / chord
change moves the signature → re-renders during the sweep only.

This test pins both halves of correct behavior, on BOTH layouts (lean melodic AND the
full-buffer drum layout the user actually runs — that 1024-byte case is where the old per-tick
memcpy hurt most):

  1. STATIC = FREE (the regression). Arm all 16 (GRIP + full GRAVITY), let it settle, then over
     a full window assert the change-detector renders NOTHING (max_dirty == 0, duty ~0) and the
     +2 UI gap stays at the unarmed baseline. Before the fix this window re-rendered all 16 every
     tick (max_dirty == 16) and the UI gap ballooned — so a revert fails here loudly.

  2. SIGNATURE LIVE (not frozen, and the tracks really ARE armed). With the transport running,
     change GRAVITY via the no_dirty path (board.tension_set(dirty=False) — writes the global
     WITHOUT RenderDirtySetAll, simulating a held-chord change on a bus, which the harness can't
     inject). The ONLY thing that can re-render now is the signature. Assert all 16 re-render
     together (max_dirty == 16) on each change. This is the positive control: it proves the
     signature fires (so the field still sweeps) AND that the 16 tracks were genuinely armed (so
     the static==0 result above isn't a false pass from unarmed tracks).

The CORRECTNESS risk the signature carries — a missed live input → a track renders stale (wrong
pitches, silent) — is covered by the held-chord/transposer live paths, which the harness cannot
drive; those are validated on-device by ear (diag_render.py: grip a static field → UI alive;
sweep the dial → field audibly moves; play a chord under a gripped TENSION track → it tracks).

The probe is the DWT cycle counter (CMD_RENDER_PERF / board.render_perf), conflict-free vs the
TIM6 MIOS32_STOPWATCH that SEQ_STATISTICS already brackets this handler with.
"""

import time

import pytest

from harness import Board, CC

NUM_TRACKS = 16
NOTE_LAYER = 0
NOTE_STEPS = 16          # track_note_init: 16 steps x 4 layers x 1 instr (par_used = 64 bytes, lean)

# Worst-case knobs. GRAVITY (TENSION) is the cleanest arm: unlike chord_mask it needs no held
# chord on a bus — GRIP > 0 + a non-zero global gravity + a pinned scale arms the slot.
GRIP_STRESS = 127        # full grip -> tension grips every gated note
GRAVITY_FULL = 63        # full throw; the signed dial is -64..+63 (0 = detent pass-through)
GRAVITY_ALT = 30         # a different non-zero throw -> a different signature (for the liveness toggles)
SCALE_IDX = 0            # any real scale; just needs a non-empty band at full gravity
SCALE_ROOT = 0           # C

LIVENESS_TOGGLES = 6     # no_dirty GRAVITY changes during the liveness window
TOGGLE_GAP_SECS = 0.04   # >> one tick (~1.1 ms @ 140 BPM) so each change lands on its own render pass

# Service-gap slack (ISR ticks) over the unarmed baseline before we call it starvation. The +2
# UI gap is THE requirement (the on-device failure is the control surface going dark = the +2
# task starving). With change-detection a STATIC armed field renders nothing, so this should sit
# right at the baseline; the margin is generous headroom, not a tuned threshold.
UI_GAP_MARGIN = 32       # UI (+2) slack — small = alive; a starved UI balloons to 100s
GAP_MARGIN = 6           # emission (+4) slack (secondary; audio degrades before the UI dies)

WINDOW_SECS = 1.5        # static measurement window (~1300 ticks @ 140 BPM): a strong "renders nothing" claim
SPINUP_SECS = 0.3        # absorb the transport-start transient + the 50 ms arming sweep window


def _setup_track(board: Board, track: int, layout: str) -> None:
    """Arm-able track. 'note' = a lean melodic line (track_note_init, par_used 64 B) with a note
    on every step. 'drum' = the full-buffer drum layout (track_drum_init, 64x1x16 = 1024 par
    bytes) — the case the user actually runs, where the old per-tick memcpy was most expensive.
    GRIP is left at 0 here; the test arms it.

    Gates are deliberately NOT set: the render cost the probe measures is gate-independent, but
    gates would make all 16 tracks emit a NoteOn flood that congests MIDI-out and delays the
    transport SysEx ack. The signature/dirty mechanics this test asserts are note- AND
    gate-independent (an armed track is force-dirty-eligible whether or not it has notes)."""
    if layout == "note":
        board.track_note_init(track)
        board.set_force_scale(track, False)               # POC rule: FTS off on gripped tracks
        for step in range(NOTE_STEPS):
            board.track_par_set(track, NOTE_LAYER, 0, step, 48 + (step % 24))
    elif layout == "drum":
        board.track_drum_init(track)                      # full-MAX 1024-byte par layout
    else:
        raise ValueError(f"unknown layout: {layout}")


def _grip_all(board: Board, grip: int) -> None:
    for track in range(NUM_TRACKS):
        board.cc_set(track, CC.TENSION_GRIP, grip)


def _wait_clock_advancing(board: Board, timeout: float = 3.0) -> None:
    """Block until the transport's bpm_tick is actually advancing."""
    t0 = board.tick_query()["bpm_tick"]
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        time.sleep(0.05)
        if board.tick_query()["bpm_tick"] != t0:
            return
    raise RuntimeError("clock did not advance after transport start")


def _measure_static(board: Board) -> dict:
    """Run the transport for one window with the field held STATIC, return the render_perf read.

    Generous transport timeout: a regression (force-dirty restored) makes the +4 render starve
    the +3 SysEx task so the transport ack itself goes slow — a tight 1.0 s would flake on the
    very failure we test for."""
    board.transport(start=True, timeout=8.0)
    _wait_clock_advancing(board)
    time.sleep(SPINUP_SECS)            # let the start transient + the arming sweep window settle
    board.render_perf_reset()
    time.sleep(WINDOW_SECS)
    r = board.render_perf()
    board.transport(start=False, timeout=8.0)
    return r


def _measure_liveness(board: Board) -> dict:
    """Transport running + a static armed field; toggle GRAVITY via the no_dirty path so ONLY the
    signature can re-render. Return the render_perf read covering the toggles."""
    board.transport(start=True, timeout=8.0)
    _wait_clock_advancing(board)
    time.sleep(SPINUP_SECS)
    board.render_perf_reset()
    for i in range(LIVENESS_TOGGLES):
        g = GRAVITY_FULL if (i % 2 == 0) else GRAVITY_ALT
        board.tension_set(g, dirty=False)   # no RenderDirtySetAll -> only the signature can fire
        time.sleep(TOGGLE_GAP_SECS)
    r = board.render_perf()
    board.transport(start=False, timeout=8.0)
    return r


@pytest.mark.hardware
@pytest.mark.timeout(120)
@pytest.mark.parametrize("layout", ["note", "drum"])
def test_render_change_detection(board, layout):
    """A static GRAVITY field on all 16 tracks must cost ZERO renders (UI alive); a live-input
    change must still re-render all 16 (field still sweeps). Pinned on both the lean melodic and
    the full-buffer drum layout the user runs."""
    board.reset()

    # 16 identical arm-able tracks + a pinned scale (the field needs a scale to snap to).
    board.global_scale_set(SCALE_IDX, root_selection=SCALE_ROOT + 1, keyb_root=0)
    for track in range(NUM_TRACKS):
        _setup_track(board, track, layout)

    # BASELINE — GRIP 0 => not armed => render on events only (the healthy UI-gap reference).
    _grip_all(board, 0)
    board.tension_set(0)
    base = _measure_static(board)

    # STATIC ARMED — GRIP full + full GRAVITY => armed, but the field is held still. With
    # change-detection this must render NOTHING over the whole window.
    _grip_all(board, GRIP_STRESS)
    board.tension_set(GRAVITY_FULL)
    static = _measure_static(board)

    # SIGNATURE LIVE — toggle GRAVITY via the no_dirty path: only the signature can re-render.
    live = _measure_liveness(board)

    diag = (
        f"render change-detection (play-readiness #5), layout={layout}:\n"
        f"  config            = {NUM_TRACKS} tracks, GRIP={GRIP_STRESS}, GRAVITY={GRAVITY_FULL}\n"
        f"  baseline (GRIP 0) : ticks={base['tick_count']} max_dirty={base['max_dirty']} "
        f"total_us={base['total_us']} ui_gap={base['ui_gap']} emit_gap={base['emission_gap']} duty={base['duty']*100:.2f}%\n"
        f"  STATIC armed      : ticks={static['tick_count']} max_dirty={static['max_dirty']} "
        f"total_us={static['total_us']} ui_gap={static['ui_gap']} emit_gap={static['emission_gap']} duty={static['duty']*100:.2f}%\n"
        f"  LIVENESS (no_dirty toggles x{LIVENESS_TOGGLES}) : ticks={live['tick_count']} "
        f"max_dirty={live['max_dirty']} max_track_us={live['max_track_us']} max_tick_us={live['max_tick_us']}\n"
        f"  thresholds        = static.max_dirty==0, static.ui_gap<=base+{UI_GAP_MARGIN}, "
        f"live.max_dirty=={NUM_TRACKS}, live.tick_count>={LIVENESS_TOGGLES - 1}"
    )
    print("\n" + diag)

    assert base["running"] and static["running"] and live["running"], (
        f"transport not running across a pass: base={base['running']} static={static['running']} "
        f"live={live['running']}\n" + diag
    )

    # POSITIVE CONTROL FIRST — prove the 16 tracks were genuinely armed AND the signature fires
    # on a no-dirty live-input change. Without this, static.max_dirty==0 could be a false pass
    # from unarmed tracks (trivially never force-dirty).
    assert live["max_dirty"] == NUM_TRACKS, (
        f"signature did NOT re-render all {NUM_TRACKS} armed tracks on a live-input change "
        f"(peak {live['max_dirty']} in one pass). Either the tracks did not arm, or the "
        f"change-detection signature is missing GRAVITY (the field would be frozen / stale).\n" + diag
    )
    assert live["tick_count"] >= LIVENESS_TOGGLES - 1, (
        f"signature fired on only {live['tick_count']} of {LIVENESS_TOGGLES} GRAVITY changes — "
        f"the change-detector is missing changes (a sweep would stutter / go stale).\n" + diag
    )

    # THE REGRESSION PIN — a STATIC armed field must re-render NOTHING. Before change-detection
    # this window re-rendered all 16 tracks EVERY tick (max_dirty == 16) and pegged the CPU.
    assert static["max_dirty"] == 0, (
        f"STATIC armed field re-rendered up to {static['max_dirty']} tracks/tick over a "
        f"{WINDOW_SECS}s window — change-detection should render NOTHING when no live input "
        f"changes. The unconditional per-tick force-dirty is back (the bug that starved the UI).\n" + diag
    )

    # THE REQUIREMENT — the +2 UI task stays alive: a static armed field keeps the UI gap at the
    # unarmed baseline (the on-device "control surface goes dark" failure, in regression form).
    assert static["ui_gap"] <= base["ui_gap"] + UI_GAP_MARGIN, (
        f"UI STARVED: +2 UI-task gap {static['ui_gap']} ISR ticks under a STATIC all-16 GRAVITY "
        f"field vs {base['ui_gap']} baseline (+{UI_GAP_MARGIN} allowed). The control surface is "
        f"going dark even though nothing is changing — change-detection is not holding.\n" + diag
    )

    # Emission (+4) likewise stays at baseline (secondary; audio degrades before the UI dies).
    assert static["emission_gap"] <= base["emission_gap"] + GAP_MARGIN, (
        f"emission starved: +4 gap {static['emission_gap']} ISR ticks under a static field vs "
        f"{base['emission_gap']} baseline (+{GAP_MARGIN} allowed).\n" + diag
    )
