// $Id$
/*
 * Header file for pattern routines
 *
 * ==========================================================================
 *
 *  Copyright (C) 2008 Thorsten Klose (tk@midibox.org)
 *  Licensed for personal non-commercial use only.
 *  All other rights reserved.
 * 
 * ==========================================================================
 */

#ifndef _SEQ_PATTERN_H
#define _SEQ_PATTERN_H


#include "seq_core.h"


/////////////////////////////////////////////////////////////////////////////
// Global definitions
/////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////
// Global Types
/////////////////////////////////////////////////////////////////////////////

typedef union {
  u16 ALL;
  struct {
    u8 pattern:7;      // full pattern number
    u8 DISABLED:1;     // pattern can be disabled
    u8 REQ:1;          // change pattern request flag
    u8 SYNCHED:1;      // change should be synched to measure
    u8 bank:3;         // pattern bank
  };
  struct {
    u8 num:3;          // pattern number (1-8)
    u8 group:3;        // pattern group (A-H)
    u8 lower:1;        // selects between upper (A-H) and lower (a-h) group
  };
} seq_pattern_t;


/////////////////////////////////////////////////////////////////////////////
// Prototypes
/////////////////////////////////////////////////////////////////////////////

extern s32 SEQ_PATTERN_Init(u32 mode);

extern char *SEQ_PATTERN_NameGet(u8 group);
extern s32 SEQ_PATTERN_Change(u8 group, seq_pattern_t pattern, u8 force_immediate_change);
extern s32 SEQ_PATTERN_Handler(void);

extern s32 SEQ_PATTERN_Load(u8 group, seq_pattern_t pattern);
extern s32 SEQ_PATTERN_Save(u8 group, seq_pattern_t pattern);

extern void SEQ_PATTERN_DirtySetTrack(u8 track);
extern void SEQ_PATTERN_DirtyClearGroup(u8 group);
extern s32 SEQ_PATTERN_WritebackIfDirty(u8 group);
extern s32 SEQ_PATTERN_WritebackAllDirty(void);

// FEARLESS SWITCHING Stage C — CHECKPOINT / REVERT (the blessed anchor)
extern s32 SEQ_PATTERN_AnchorPresent(void);
extern s32 SEQ_PATTERN_Checkpoint(void);
extern s32 SEQ_PATTERN_Revert(void);

// REVERT-undoable (Stage 2b): the unified journal's ORGANISM scope drives these.
// SEQ_PATTERN_Revert stashes the live jam into a pre-revert anchor-bank slot
// before restoring the checkpoint; RevertUndoRead restores that stashed jam,
// RevertRedoRead re-restores the checkpoint. Both go through SnapshotRead (which
// invalidates the journal — SEQ_CORE re-arms its state afterwards).
extern s32 SEQ_PATTERN_RevertUndoRead(void);
extern s32 SEQ_PATTERN_RevertRedoRead(void);

// PHRASES — the snapshot library (capture/recall generalize CHECKPOINT/REVERT)
extern s32 SEQ_PATTERN_PhraseCapture(u8 n);
extern s32 SEQ_PATTERN_PhraseRecall(u8 n);
extern s32 SEQ_PATTERN_PhrasePresent(u8 n);
extern u16 SEQ_PATTERN_PhrasePresentMask(void);
extern s32 SEQ_PATTERN_PhraseLastRecalled(void);
extern s32 SEQ_PATTERN_PhraseDrifted(void);
extern char *SEQ_PATTERN_PhraseName(u8 n);
extern s32 SEQ_PATTERN_PhraseNameCommit(u8 n);
extern void SEQ_PATTERN_PhraseResetState(void);
extern void SEQ_PATTERN_ProbePhrasesOnLoad(void);

// POSTURE-MORPH (Loop A) — glide ONE group's posture (ext CCs 0x80..0x9f) from
// its live state (pos 0, true pass-through) toward a target phrase's same-group
// slice (pos PHRASE_MORPH_MAX). live = A + pos/MAX*(B-A), applied per-measure.
// Grid/notes untouched; the other 3 groups untouched. See design §10.
//
// SEQ_PHRASE_MORPH gates the whole feature (the ~7.7 KB arm/target buffers + the
// per-measure apply). Default ON; `make PHRASE_MORPH=0` compiles it out to reclaim
// the RAM — the 7 entry points below become stubs (Target() returns -1, so every UI
// morph intercept falls through). By-ear CUT candidate, design §9 2026-06-22. The
// extern decls stay unconditional so the call sites link either way; PHRASE_MORPH_MAX
// stays defined (seq_ui.c references it unconditionally).
#ifndef SEQ_PHRASE_MORPH
#define SEQ_PHRASE_MORPH 1
#endif
#define PHRASE_MORPH_MAX 16
extern s32 SEQ_PATTERN_PhraseMorphArm(u8 n);    // arm target B = phrase n, group = ui_selected_group
extern s32 SEQ_PATTERN_PhraseMorphSet(u8 v);    // set pos 0..PHRASE_MORPH_MAX (defers apply to boundary)
extern s32 SEQ_PATTERN_PhraseMorphTick(void);   // PER-MEASURE: if dirty, lerp+set+rerender the focused group
extern void SEQ_PATTERN_PhraseMorphCancel(void);// disarm (does NOT revert live CCs — leave-as-live)
extern void SEQ_PATTERN_PhraseMorphInvalidateGroup(u8 group); // disarm if `group` is the morph's (out-of-band CC replace)
extern s32 SEQ_PATTERN_PhraseMorphTarget(void); // armed phrase n, or -1 if disarmed
extern u8  SEQ_PATTERN_PhraseMorphValue(void);  // current pos 0..PHRASE_MORPH_MAX

extern s32 SEQ_PATTERN_PeekName(seq_pattern_t pattern, char *pattern_name);
extern s32 SEQ_PATTERN_PeekPatternsOfGroup(seq_pattern_t pattern);

extern s32 SEQ_PATTERN_Fix(u8 group, seq_pattern_t pattern);
extern s32 SEQ_PATTERN_FixAll(void);


/////////////////////////////////////////////////////////////////////////////
// Export global variables
/////////////////////////////////////////////////////////////////////////////

extern seq_pattern_t seq_pattern[SEQ_CORE_NUM_GROUPS];
extern seq_pattern_t seq_pattern_req[SEQ_CORE_NUM_GROUPS];
extern char seq_pattern_name[SEQ_CORE_NUM_GROUPS][21];

extern mios32_sys_time_t seq_pattern_start_time;
extern u16 seq_pattern_remix_map;
extern u8 seq_pattern_log_load_time;

// FEARLESS SWITCHING (design §9 2026-06-11 save-model inversion): live state
// diverged from the group's working slot. Bit per group. Set by the source-
// write chokepoints (SEQ_PAR_Set / SEQ_TRG_Set* / SEQ_CC_Set) + the direct
// writers that bypass them; cleared when slot==live again (group load, save
// to the working slot, writeback).
extern u8 seq_pattern_dirty;
extern u32 seq_pattern_writeback_count; // diagnostic (HIL pins writeback-fired/skipped)

#endif /* _SEQ_PATTERN_H */
