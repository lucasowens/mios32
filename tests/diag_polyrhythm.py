"""Diagnostic for the polyrhythm gate-path puzzle (xfail test_polyrhythm_3_in_8).

What this does:
  1. Reset device, switch track 0 to USB1, open TRKEUCLID.
  2. Set generator type to POLY(3, 8).
  3. Read raw trigger bytes (both seq_trg_layer_value source and the phase-A
     seq_trg_output_value mirror) so we can see what's actually stored.
  4. Read the LCD line that shows the gate-trigger row.
  5. PLAY for ~3s and capture Note Ons.
  6. Print stored-pattern vs captured-note-timeline side-by-side so the
     mismatch is obvious from one screenful of output.

This isn't a pytest — run directly:
    cd tests && bash -c 'source .venv/bin/activate && python diag_polyrhythm.py'
"""

import time

from harness import Board, Button, Encoder, MidiPort, Page

GEN_TYPE_COUNT = 5
GEN_POLY = 2


def _bits_to_steps_str(layer_bytes: bytes, n_steps: int = 16) -> str:
    """Render 16 steps as 'X' (gate on) / '.' (off). LSB of byte 0 = step 1."""
    out = []
    for s in range(n_steps):
        byte = layer_bytes[s // 8] if s // 8 < len(layer_bytes) else 0
        bit = (byte >> (s % 8)) & 1
        out.append("X" if bit else ".")
    return "".join(out)


def _select_gen_type(board, gen_type: int) -> None:
    board.turn(Encoder.GP(12), -(GEN_TYPE_COUNT + 2))
    if gen_type > 0:
        board.turn(Encoder.GP(12), +gen_type)
    time.sleep(0.1)


def main():
    with Board() as board:
        print("PING:", board.ping())
        board.reset()
        board.track_config(track=0, midi_port=MidiPort.USB0, channel=0)
        board.page_set(Page.TRKEUCLID)
        time.sleep(0.1)

        _select_gen_type(board, GEN_POLY)

        # Polyrhythm: N=3, M=8. Floor then dial up.
        board.turn(Encoder.GP(9), -99)
        board.turn(Encoder.GP(10), -99)
        board.turn(Encoder.GP(10), +8)
        board.turn(Encoder.GP(9), +3)
        time.sleep(0.2)

        # What's the gate-trigger layer assignment for track 0?
        from harness.sysex import CC
        gate_asg = board.cc_get(0, CC.ASG_GATE)
        accent_asg = board.cc_get(0, CC.ASG_GATE + 1)
        roll_asg = board.cc_get(0, CC.ASG_GATE + 2)
        glide_asg = board.cc_get(0, CC.ASG_GATE + 3)
        skip_asg = board.cc_get(0, CC.ASG_GATE + 4)
        print(f"\ntrack 0 trg assignments:")
        print(f"  gate    = {gate_asg}  (0=none, 1..8 = layer 0..7)")
        print(f"  accent  = {accent_asg}")
        print(f"  roll    = {roll_asg}")
        print(f"  glide   = {glide_asg}")
        print(f"  skip    = {skip_asg}")

        # Dump every trigger layer (0..7) for the first 16 steps (2 bytes each).
        print(f"\ntrigger layers (track 0, 16 steps):")
        print(f"  layer  source           mirror           role")
        for lyr in range(8):
            try:
                src, out = board.trg_byte_get(track=0, trg_layer=lyr, step8_count=2)
            except RuntimeError as e:
                print(f"  {lyr}      <error: {e}>")
                continue
            role = []
            if gate_asg - 1 == lyr:   role.append("GATE")
            if accent_asg - 1 == lyr: role.append("accent")
            if roll_asg - 1 == lyr:   role.append("roll")
            if glide_asg - 1 == lyr:  role.append("glide")
            if skip_asg - 1 == lyr:   role.append("skip")
            role_str = ",".join(role) or "-"
            print(f"  {lyr}      {_bits_to_steps_str(src)} {_bits_to_steps_str(out)} {role_str}")

        # LCD view (TRKEUCLID's gate row is line 1).
        snap = board.lcd_snapshot()
        print(f"\nLCD line 0: {snap.line(0)}")
        print(f"LCD line 1: {snap.line(1)}")

        # PLAY and capture.
        print(f"\nPLAY for 3s, capturing Note Ons on ch 0...")
        capture_t0 = board.capture_start()
        board.press(Button.PLAY)
        time.sleep(3.0)
        board.press(Button.STOP)
        time.sleep(0.1)

        notes = [
            e for e in board.capture_notes(since=capture_t0)
            if e.is_on and e.channel == 0
        ]
        print(f"  captured {len(notes)} Note Ons")
        for e in notes[:32]:
            t = e.timestamp - capture_t0
            print(f"    t={t:7.3f}s  note={e.note}  vel={e.velocity}")

        if len(notes) >= 2:
            intervals = [
                notes[i+1].timestamp - notes[i].timestamp
                for i in range(len(notes) - 1)
            ]
            print(f"\n  intervals (s): {[round(i, 3) for i in intervals[:32]]}")
            print(f"  min={min(intervals):.3f}  max={max(intervals):.3f}  "
                  f"mean={sum(intervals)/len(intervals):.3f}")


if __name__ == "__main__":
    main()
