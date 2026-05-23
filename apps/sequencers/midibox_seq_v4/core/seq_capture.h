// $Id$
/*
 * Emission tap + pattern-slot commit ("bounce-in-place")
 *
 * Captures live-emitted MIDI events from a track into a per-track ring
 * buffer. On user request, replays the last N measures of captured events
 * into a destination pattern slot in the same group, so the user can audition
 * generative output by switching patterns (A1 -> A2 -> A3 ...).
 *
 * The tap site is one line in SEQ_CORE_ScheduleEvent (seq_core.c). When no
 * track is armed, the cost is one branch on a hot u8. When armed, it is a
 * single packed write to a static ring. Commit work runs in user-task
 * context, not in the tick path.
 *
 * ==========================================================================
 *
 *  Copyright (C) 2026 Lucas Owens (lowens@outcomemd.com)
 *  Licensed for personal non-commercial use only.
 *  All other rights reserved.
 *
 * ==========================================================================
 */

#ifndef _SEQ_CAPTURE_H
#define _SEQ_CAPTURE_H

#include <mios32.h>
#include "seq_core.h"
#include "seq_pattern.h"


/////////////////////////////////////////////////////////////////////////////
// Global definitions
/////////////////////////////////////////////////////////////////////////////

// 128 events per track x 6 bytes = 768B per track; 16 tracks => 12 KB total
#define SEQ_CAPTURE_RING_SIZE 128


/////////////////////////////////////////////////////////////////////////////
// Global types
/////////////////////////////////////////////////////////////////////////////

// packed ring entry; one MIDI event with a 32-bit BPM-tick timestamp
typedef struct {
  u32 bpm_tick;  // absolute BPM tick at emission time
  u8  status;    // MIDI status nibble + channel (i.e. midi_package.evnt0)
  u8  data1;     // note / cc number
  u8  data2;     // velocity / cc value
  u8  flags;     // reserved (e.g. echo marker); 0 = normal live event
} seq_capture_event_t;


/////////////////////////////////////////////////////////////////////////////
// Prototypes
/////////////////////////////////////////////////////////////////////////////

extern s32 SEQ_CAPTURE_Init(u32 mode);

// per-track arm; tap is a no-op when the track is not armed
extern s32 SEQ_CAPTURE_ArmTrack(u8 track, u8 armed);
extern u8  SEQ_CAPTURE_IsArmed(u8 track);

// hot path; called from SEQ_CORE_ScheduleEvent. Filters non-musical events
// (clock/active-sense/sysex) and writes one ring entry on note/CC/etc.
extern s32 SEQ_CAPTURE_TapEvent(u8 track, mios32_midi_package_t pkg, u32 bpm_tick);

// scan src_group for the first empty pattern slot != current_pattern.
// Returns the pattern index (0..7) or -1 if none free.
extern s32 SEQ_CAPTURE_FindNextFreeSlot(u8 group, u8 current_pattern);

// reset the ring for a track (used after commit, or by user-initiated clear)
extern s32 SEQ_CAPTURE_Clear(u8 track);

// commit the last `num_measures` of captured events on src_track into the
// destination pattern slot. The current pattern is preserved (saved first,
// then reloaded after the write).
//   dst_pattern: encoded the same way as seq_pattern_t (bits 0..6)
//   Returns 0 on success, negative on error.
extern s32 SEQ_CAPTURE_CommitToSlot(u8 src_track, u8 dst_group, seq_pattern_t dst_pattern, u8 num_measures);


#endif /* _SEQ_CAPTURE_H */
