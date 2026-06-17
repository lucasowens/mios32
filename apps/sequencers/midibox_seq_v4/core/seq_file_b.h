// $Id$
/*
 * Header for file functions
 *
 * ==========================================================================
 *
 *  Copyright (C) 2008 Thorsten Klose (tk@midibox.org)
 *  Licensed for personal non-commercial use only.
 *  All other rights reserved.
 * 
 * ==========================================================================
 */

#ifndef _SEQ_FILE_B_H
#define _SEQ_FILE_B_H


/////////////////////////////////////////////////////////////////////////////
// Global definitions
/////////////////////////////////////////////////////////////////////////////

#define SEQ_FILE_B_NUM_BANKS 4

// FEARLESS SWITCHING Stage C: the CHECKPOINT/REVERT anchor rides the bank
// record format as an internal fifth "bank" addressed by this sentinel. It is
// OUTSIDE the 0..NUM_BANKS-1 range on purpose, so every for(bank<NUM_BANKS)
// loop (load-all / unload-all / save-all) skips it — the anchor is never
// auto-loaded, never written by a session save, and not user-navigable.
#define SEQ_FILE_B_ANCHOR_BANK 0xfe

// PHRASES bundle: the snapshot library rides the same bank record format as a
// second internal sentinel "bank" (file MBSEQ_PH.V4), holding 16 whole-organism
// phrases laid out as 16 * 4 group-records (phrase N -> patterns 4N..4N+3).
// Like the anchor it is OUTSIDE 0..NUM_BANKS-1 so every for(bank<NUM_BANKS) loop
// skips it: never auto-loaded/saved, not user-navigable, survives session
// writeback untouched. Capture = CHECKPOINT generalized; recall = REVERT
// generalized (see SEQ_PATTERN_PhraseCapture / _PhraseRecall).
#define SEQ_FILE_B_PHRASE_BANK 0xfd

// 16 phrases, each a 4-group snapshot -> exactly fills a 64-pattern bank.
#define SEQ_FILE_B_NUM_PHRASES 16

// The ext-CC "posture" block carried in every track record: CC 0x80..0x9f
// (robotize mask/probabilities, chord-mask 0x96..0x99, tension GRIP 0x9a, ...).
// Public because SEQ_FILE_B_PhraseReadCCs fills exactly SEQ_FILE_B_TRK_EXT_CC_COUNT
// bytes (the caller's cc_out must be at least that big) and the posture-morph
// reads/writes this CC range live via SEQ_CC_Get/Set(track, _CC_FIRST + i).
#define SEQ_FILE_B_TRK_EXT_CC_FIRST     0x80
#define SEQ_FILE_B_TRK_EXT_CC_LAST      0x9f   // widened past GRIP (0x9a); a clean boundary with headroom
#define SEQ_FILE_B_TRK_EXT_CC_COUNT     (SEQ_FILE_B_TRK_EXT_CC_LAST - SEQ_FILE_B_TRK_EXT_CC_FIRST + 1)  // 32 (V3)


/////////////////////////////////////////////////////////////////////////////
// Global Types
/////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////
// Prototypes
/////////////////////////////////////////////////////////////////////////////

extern s32 SEQ_FILE_B_Init(u32 mode);
extern s32 SEQ_FILE_B_LoadAllBanks(char *session);
extern s32 SEQ_FILE_B_UnloadAllBanks(void);
extern s32 SEQ_FILE_B_SaveAllBanks(char *session);

extern s32 SEQ_FILE_B_NumPatterns(u8 bank);

extern s32 SEQ_FILE_B_Create(char *session, u8 bank);
extern s32 SEQ_FILE_B_Open(char *session, u8 bank);

extern s32 SEQ_FILE_B_PatternRead(u8 bank, u8 pattern, u8 target_group,  u16 remix_map);
extern s32 SEQ_FILE_B_TrackRead(u8 bank, u8 pattern, u8 slot_track, u8 dst_track);

// POSTURE-MORPH: read ONLY one section's ext-CC block (0x80..0x9f, 32 bytes)
// into cc_out, with zero live writes — the morph endpoint reader. V3/V4 -> 32
// CCs; V2 -> 22 CCs zero-extended; V1/absent/mismatch -> error (caller refuses).
extern s32 SEQ_FILE_B_PhraseReadCCs(u8 bank, u8 pattern, u8 slot_track, u8 *cc_out);
extern s32 SEQ_FILE_B_PatternWrite(char *session, u8 bank, u8 pattern, u8 source_group, u8 rename_if_empty_name);
extern s32 SEQ_FILE_B_PatternWriteEmpty(char *session, u8 bank, u8 pattern);

extern s32 SEQ_FILE_B_PatternPeekName(u8 bank, u8 pattern, u8 non_cached, char *pattern_name);

// PHRASES cross-session occupancy probe (returns a 16-bit mask, >= 0; 0 if no
// file). `names` (NULL to skip) is filled with each occupied phrase's 20-char
// base-record name (NUL-terminated), so names survive a session reload.
extern s32 SEQ_FILE_B_PhraseOccupancyProbe(char *session, char (*names)[21]);

// PHRASES: overwrite only the name field of phrase n's base record (capture
// stamp / rename-without-recapture); leaves the captured organism untouched.
extern s32 SEQ_FILE_B_PhraseWriteName(char *session, u8 n, char *name);


/////////////////////////////////////////////////////////////////////////////
// Export global variables
/////////////////////////////////////////////////////////////////////////////


#endif /* _SEQ_FILE_B_H */
