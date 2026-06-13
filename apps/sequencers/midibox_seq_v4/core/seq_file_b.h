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
extern s32 SEQ_FILE_B_PatternWrite(char *session, u8 bank, u8 pattern, u8 source_group, u8 rename_if_empty_name);

extern s32 SEQ_FILE_B_PatternPeekName(u8 bank, u8 pattern, u8 non_cached, char *pattern_name);


/////////////////////////////////////////////////////////////////////////////
// Export global variables
/////////////////////////////////////////////////////////////////////////////


#endif /* _SEQ_FILE_B_H */
