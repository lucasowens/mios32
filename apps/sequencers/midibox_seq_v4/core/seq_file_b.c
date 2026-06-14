// $Id$
/*
 * Bank access functions
 *
 * NOTE: before accessing the SD Card, the upper level function should
 * synchronize with the SD Card semaphore!
 *   MUTEX_SDCARD_TAKE; // to take the semaphore
 *   MUTEX_SDCARD_GIVE; // to release the semaphore
 *
 * ==========================================================================
 *
 *  Copyright (C) 2008 Thorsten Klose (tk@midibox.org)
 *  Licensed for personal non-commercial use only.
 *  All other rights reserved.
 * 
 * ==========================================================================
 */

/////////////////////////////////////////////////////////////////////////////
// Include files
/////////////////////////////////////////////////////////////////////////////

#include <mios32.h>

#include <string.h>

#include "file.h"
#include "seq_file.h"
#include "seq_file_b.h"

#include "seq_core.h"
#include "seq_cc.h"
#include "seq_par.h"
#include "seq_trg.h"
#include "seq_pattern.h"
#include "seq_generator.h"


/////////////////////////////////////////////////////////////////////////////
// for optional debugging messages via DEBUG_MSG (defined in mios32_config.h)
/////////////////////////////////////////////////////////////////////////////
#define DEBUG_VERBOSE_LEVEL 0


/////////////////////////////////////////////////////////////////////////////
// per-track extension block, appended after all tracks within a pattern
// slot (before zero-fill). lets fields outside the 128-byte CC array
// persist without bumping the file magic. the in-file cc[128] block covers
// CC numbers 0x00..0x7f only - anything higher (the robotize feature lives
// at 0x80..0x95) was never persisted by the original format. this ext block
// stores those CC values plus the runtime-derived robotize bar anchors.
//
// older firmware reading a new file sees the ext bytes as harmless trailing
// pad; new firmware reading an old file sees a zero byte where the tag
// would be and leaves the in-RAM values alone.
//
// v1 (tag 0x01) used briefly during dev: anchors only, 64 bytes. v2 adds
// the extended CC block. read path accepts both.
/////////////////////////////////////////////////////////////////////////////
#define SEQ_FILE_B_TRK_EXT_TAG_V1       0x01  // anchors only (legacy)
#define SEQ_FILE_B_TRK_EXT_TAG_V2       0x02  // ext CCs 0x80..0x95 + anchors
#define SEQ_FILE_B_TRK_EXT_TAG_V3       0x03  // ext CCs 0x80..0x9f (adds chord-mask 0x96..0x99 + tension GRIP 0x9a) + anchors
#define SEQ_FILE_B_TRK_EXT_TAG_V4       0x04  // V3 payload + generator sub-block (FEARLESS SWITCHING Stage B)

#define SEQ_FILE_B_TRK_EXT_CC_FIRST     0x80
#define SEQ_FILE_B_TRK_EXT_CC_LAST      0x9f   // widened past GRIP (0x9a); a clean boundary with headroom
#define SEQ_FILE_B_TRK_EXT_CC_COUNT     (SEQ_FILE_B_TRK_EXT_CC_LAST - SEQ_FILE_B_TRK_EXT_CC_FIRST + 1)  // 32 (V3)
// V2 count is FROZEN: V2 files on SD hold exactly this many ext-CC bytes, so the
// V2 read arm must keep using it even though the live range above grew. (Bumping
// LAST without freezing this would mis-align every V2 pattern's anchors on read.)
#define SEQ_FILE_B_TRK_EXT_CC_COUNT_V2  (0x95 - 0x80 + 1)  // 22 (V2, frozen)
#define SEQ_FILE_B_TRK_EXT_ANCHORS_SIZE 64    // sizeof(robotize_bar_anchors)
#define SEQ_FILE_B_TRK_EXT_V1_SIZE      (1 + SEQ_FILE_B_TRK_EXT_ANCHORS_SIZE)
#define SEQ_FILE_B_TRK_EXT_V2_SIZE      (1 + SEQ_FILE_B_TRK_EXT_CC_COUNT_V2 + SEQ_FILE_B_TRK_EXT_ANCHORS_SIZE)
#define SEQ_FILE_B_TRK_EXT_V3_SIZE      (1 + SEQ_FILE_B_TRK_EXT_CC_COUNT + SEQ_FILE_B_TRK_EXT_ANCHORS_SIZE)

// V4 generator sub-block (FEARLESS SWITCHING Stage B — "the organism comes
// back alive"): appended after the V3 payload. Fixed-size region — count byte
// + SEQ_GENERATOR_PERSIST_SLOTS entry slots, unused entries zero-filled — so
// the per-track ext stride stays constant (SEQ_FILE_B_TrackRead indexes ext
// blocks by stride). Entry = 9 header bytes (instrument, par_layer, engaged,
// range_min/max, rate, depth, contour, anchor_valid) + loop + locks + anchor
// + mult = 177 bytes.
#define SEQ_FILE_B_TRK_EXT_GEN_ENTRY_SIZE (9 + SEQ_GENERATOR_LOOP_LEN + SEQ_GENERATOR_LOCKS_BYTES \
                                           + SEQ_GENERATOR_LOOP_LEN + SEQ_GENERATOR_MULT_BYTES)  // 177
#define SEQ_FILE_B_TRK_EXT_GEN_BLOCK_SIZE (1 + SEQ_GENERATOR_PERSIST_SLOTS*SEQ_FILE_B_TRK_EXT_GEN_ENTRY_SIZE)  // 709
#define SEQ_FILE_B_TRK_EXT_V4_SIZE      (SEQ_FILE_B_TRK_EXT_V3_SIZE + SEQ_FILE_B_TRK_EXT_GEN_BLOCK_SIZE)  // 806

// Slots reserve room for the CURRENT (V4) ext block at create time. Banks
// created by older firmware reserved only the older sizes — the write path
// degrades per record: V4 if the slot has room, else V3 (gen state won't
// persist there), else no ext at all. Recreate the session/bank
// (CMD_SESSION_CREATE / CMD_BANK_CREATE) to get V4-sized slots.
#define SEQ_FILE_B_TRK_EXT_SIZE         SEQ_FILE_B_TRK_EXT_V4_SIZE


/////////////////////////////////////////////////////////////////////////////
// Local types
/////////////////////////////////////////////////////////////////////////////

// Structure of bank file:
//    file_type[10]
//    seq_file_b_header_t
//    Pattern 0: seq_file_b_pattern_t
//               Track 0: seq_file_b_track_t
//                        Parameter Layers (num_p_instruments * num_p_layers * p_layer_size)
//                        Trigger Layers (num_t_instruments * num_t_layers * t_layer_size)
//               Track 1: ...
//    Pattern 1: ...
//
// Size for 64 patterns, 64*4 tracks,
// each consists of 1 instrument, 16 parameter and 8 trigger layers (format is flexible enough to change these numbers)
// parameter layer has 64, trigger layer 256 steps (for easier partitioning, see comments below)
// 10 + 24 + 64 * (24 + 4 * (216 + 1*16*64 + 1*8*256/8))
// 10 + 24 + 64 * (24 + 4 * (216 + 1024  + 256))
// 10 + 24 + 64 * (24 + 4 * 1496)
// 10 + 24 + 64 * (24 + 5984)
// 10 + 24 + 384512
// -> 384546 bytes (good that SD cards are so cheap today ;)

// note: allocating more bytes will result into more FAT operations when seeking a pattern position
// e.g. I tried 16 layers/256 steps, and it took ca. 10 mS to find the starting sector
// With these settings, it takes ca. 3 mS
// The overall loading time is ca. 7 mS - pretty good for such a large bulk of data!!!
// If more steps are desired, just reduce the number of parameter layers
// e.g. 256 steps, 4 parameter layers will result into 1024 bytes as well
// this has the advantage, that a single bank can store different parameter layer configurations as long as the size is equal

// not defined as structure: 
// file_type[10] will contain "MBSEQV4_B" + 0 (zero-terminated string)
typedef struct {
  char name[20];      // bank name consists of 20 characters, no zero termination, patted with spaces
  u16  num_patterns;  // number of patterns per bank (usually 64)
  u16  pattern_size;  // reserved size for each pattern
} seq_file_b_header_t;  // 24 bytes

typedef struct {
  char name[20];      // pattern name consists of 20 characters, no zero termination, patted with spaces
  u8   num_tracks;    // number of tracks in pattern (usually 4)
  u8   mixer_map;     // link to optional mixer map (0=off)
  u8   sysex_setup;   // link to optional sysex setup (0=off)
  u8   reserved1;     // reserved for future extensions
} seq_file_b_pattern_t;  // 24 bytes

typedef struct {
  char name[80];      // track name consists of 80 characters, no zero termination, patted with spaces
  u8   num_p_instruments; // number of instruments (usually 1, or 16/8 for drum tracks)
  u8   num_t_instruments; // number of instruments (usually 1, or 16/8 for drum tracks)
  u8   num_p_layers;  // number of parameter layers (usually 4, 8 or 16)
  u8   num_t_layers;  // number of trigger layers (usually 8, or 1/2 for drum tracks)
  u16  p_layer_size;  // size per parameter layer, e.g. 256 steps
  u16  t_layer_size;  // size per trigger layer (divided by 8, as each step has it's own bit)
  u8   cc[128];       // contains all CC parameters, prepared for 128
} seq_file_b_track_t; // 156 bytes



// bank informations stored in RAM
typedef struct {
  unsigned valid: 1;  // bank is accessible

  seq_file_b_header_t header;

  file_t file;      // file informations
} seq_file_b_info_t;


/////////////////////////////////////////////////////////////////////////////
// Local prototypes
/////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////
// Local variables
/////////////////////////////////////////////////////////////////////////////

static seq_file_b_info_t seq_file_b_info[SEQ_FILE_B_NUM_BANKS];

// FEARLESS SWITCHING Stage C — the CHECKPOINT/REVERT anchor's parallel info
// slot (see SEQ_FILE_B_ANCHOR_BANK). Kept OUT of seq_file_b_info[] so it never
// participates in the user-bank load/unload/save loops; opened/created lazily
// by SEQ_PATTERN_Checkpoint / _Revert / _AnchorPresent against the current
// session, so it always tracks the live session (the file is per-session).
static seq_file_b_info_t seq_file_anc_info;

// PHRASES bundle — the snapshot library's parallel info slot (see
// SEQ_FILE_B_PHRASE_BANK), kept OUT of seq_file_b_info[] for the same reason as
// the anchor: it must never join the user-bank load/unload/save loops. Opened/
// created lazily by SEQ_PATTERN_PhraseCapture / _PhraseRecall against the
// current session.
static seq_file_b_info_t seq_file_phr_info;

static u8 cached_pattern_name[21];
static u8 cached_bank;
static u8 cached_pattern;


/////////////////////////////////////////////////////////////////////////////
// Resolve a bank index (0..NUM_BANKS-1) or the anchor sentinel to its info
// slot. Returns NULL for an out-of-range bank. Lets the record serializer
// (PatternWrite/PatternRead/TrackRead/Create/Open) be reused wholesale for the
// anchor without the anchor leaking into the four-bank arrays/loops.
/////////////////////////////////////////////////////////////////////////////
static seq_file_b_info_t *SEQ_FILE_B_InfoPtr(u8 bank)
{
  if( bank == SEQ_FILE_B_ANCHOR_BANK )
    return &seq_file_anc_info;
  if( bank == SEQ_FILE_B_PHRASE_BANK )
    return &seq_file_phr_info;
  if( bank < SEQ_FILE_B_NUM_BANKS )
    return &seq_file_b_info[bank];
  return NULL;
}

/////////////////////////////////////////////////////////////////////////////
// Build the on-SD path for a bank: user banks are MBSEQ_B1..4.V4; the anchor
// is the fixed, non-navigable MBSEQ_AN.V4 (the ".V4" is the file-format
// suffix, same as the user banks — not the V4 generator ext tag). The base
// name MUST stay <= 8 chars: FatFs runs with _USE_LFN=0 (8.3 short names
// only), so a 9-char base like "MBSEQ_ANC" is FR_INVALID_NAME and the create
// silently fails — "MBSEQ_AN" (8) is the longest that fits.
/////////////////////////////////////////////////////////////////////////////
static void SEQ_FILE_B_BuildPath(char *filepath, char *session, u8 bank)
{
  if( bank == SEQ_FILE_B_ANCHOR_BANK )
    sprintf(filepath, "%s/%s/MBSEQ_AN.V4", SEQ_FILE_SESSION_PATH, session);
  else if( bank == SEQ_FILE_B_PHRASE_BANK )
    // base "MBSEQ_PH" is 8 chars — within the FatFs _USE_LFN=0 8.3 limit (the
    // same constraint that forced MBSEQ_ANC -> MBSEQ_AN in FEARLESS).
    sprintf(filepath, "%s/%s/MBSEQ_PH.V4", SEQ_FILE_SESSION_PATH, session);
  else
    sprintf(filepath, "%s/%s/MBSEQ_B%d.V4", SEQ_FILE_SESSION_PATH, session, bank+1);
}


/////////////////////////////////////////////////////////////////////////////
// Initialisation
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_FILE_B_Init(u32 mode)
{
  // invalidate all bank infos
  SEQ_FILE_B_UnloadAllBanks();

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Loads all banks
// Called from SEQ_FILE_CheckSDCard() when the SD card has been connected
// returns < 0 on errors
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_FILE_B_LoadAllBanks(char *session)
{
  s32 status = 0;

  // load all banks
  u8 bank;
  for(bank=0; bank<SEQ_FILE_B_NUM_BANKS; ++bank) {
    s32 error = SEQ_FILE_B_Open(session, bank);
#if DEBUG_VERBOSE_LEVEL >= 1
    DEBUG_MSG("[SEQ_FILE_B] Tried to open bank #%d file, status: %d\n", bank+1, error);
#endif
#if 0
    if( error == -2 ) {
      error = SEQ_FILE_B_Create(session, bank);
#if DEBUG_VERBOSE_LEVEL >= 1
      DEBUG_MSG("[SEQ_FILE_B] Tried to create bank #%d file, status: %d\n", bank+1, error);
#endif
    }
#endif
    status |= error;
  }

  return status;
}


/////////////////////////////////////////////////////////////////////////////
// Unloads all banks
// Called from SEQ_FILE_CheckSDCard() when the SD card has been disconnected
// returns < 0 on errors
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_FILE_B_UnloadAllBanks(void)
{
  // invalidate all bank infos
  u8 bank;
  for(bank=0; bank<SEQ_FILE_B_NUM_BANKS; ++bank)
    seq_file_b_info[bank].valid = 0;

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Saves all banks
// returns < 0 on errors
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_FILE_B_SaveAllBanks(char *session)
{
  s32 status = 0;

  u8 bank;
  for(bank=0; bank<SEQ_FILE_B_NUM_BANKS; ++bank) {
    s32 error = SEQ_FILE_B_PatternWrite(seq_file_session_name, seq_pattern[bank].bank, seq_pattern[bank].pattern, bank, 1);

#if DEBUG_VERBOSE_LEVEL >= 1
    DEBUG_MSG("[SEQ_FILE_B] Tried to store bank #%d file, status: %d\n", bank+1, error);
#endif

    status |= error;
  }

  return status;
}


/////////////////////////////////////////////////////////////////////////////
// Returns number of patterns in bank
// Returns 0 if bank not valid
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_FILE_B_NumPatterns(u8 bank)
{
  if( (bank < SEQ_FILE_B_NUM_BANKS) && seq_file_b_info[bank].valid )
    return seq_file_b_info[bank].header.num_patterns;

  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// create a complete bank file
// returns < 0 on errors (error codes are documented in seq_file.h)
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_FILE_B_Create(char *session, u8 bank)
{
  seq_file_b_info_t *info = SEQ_FILE_B_InfoPtr(bank);
  if( info == NULL )
    return SEQ_FILE_B_ERR_INVALID_BANK;

  info->valid = 0; // set to invalid as long as we are not sure if file can be accessed

  char filepath[MAX_PATH];
  SEQ_FILE_B_BuildPath(filepath, session, bank);

#if DEBUG_VERBOSE_LEVEL >= 1
  DEBUG_MSG("[SEQ_FILE_B] Creating new bank file '%s'\n", filepath);
#endif

  s32 status = 0;
  if( (status=FILE_WriteOpen(filepath, 1)) < 0 ) {
#if DEBUG_VERBOSE_LEVEL >= 1
    DEBUG_MSG("[SEQ_FILE_B] Failed to create file, status: %d\n", status);
#endif
    return status;
  }

  // write seq_file_b_header
  const char file_type[10] = "MBSEQV4_B";
  status |= FILE_WriteBuffer((u8 *)file_type, 10);

  // write bank name w/o zero terminator
  char bank_name[21];
  sprintf(bank_name, "Default Bank        ");
  memcpy(info->header.name, bank_name, 20);
  status |= FILE_WriteBuffer((u8 *)info->header.name, 20);
#if DEBUG_VERBOSE_LEVEL >= 1
  DEBUG_MSG("[SEQ_FILE_B] writing '%s'...\n", bank_name);
#endif

  // number of patterns
  info->header.num_patterns = 64;
  status |= FILE_WriteHWord(info->header.num_patterns);

  // write predefined pattern size
  u8 num_tracks = 4;
  u8 num_p_instruments = 1;
  u8 num_t_instruments = 1;
  u8 num_p_layers = 16;
  u16 num_t_layers = 8;
  u16 p_layer_size = 64; // 256 steps if 4 layers
  u16 t_layer_size = 256/8;

  info->header.pattern_size = sizeof(seq_file_b_pattern_t) +
    num_tracks * (sizeof(seq_file_b_track_t) + num_p_instruments*num_p_layers*p_layer_size + num_t_instruments*num_t_layers*t_layer_size + SEQ_FILE_B_TRK_EXT_SIZE);
  status |= FILE_WriteHWord(info->header.pattern_size);

  // not required anymore with FatFs (was required with DOSFS)
#if 0
  // create empty pattern slots
  u32 pattern;
  for(pattern=0; pattern<info->header.num_patterns; ++pattern) {
    u32 pos;
    for(pos=0; pos<info->header.pattern_size; ++pos)
      status |= FILE_WriteByte(0x00);
  }
#endif

  // close file
  status |= FILE_WriteClose();

  if( status >= 0 )
    // bank valid - caller should fill the pattern slots with useful data now
    info->valid = 1;


#if DEBUG_VERBOSE_LEVEL >= 1
  DEBUG_MSG("[SEQ_FILE_B] Bank file created with status %d\n", status);
#endif

  return status;
}


/////////////////////////////////////////////////////////////////////////////
// open a bank file
// returns < 0 on errors (error codes are documented in seq_file.h)
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_FILE_B_Open(char *session, u8 bank)
{
  seq_file_b_info_t *info = SEQ_FILE_B_InfoPtr(bank);
  if( info == NULL )
    return SEQ_FILE_B_ERR_INVALID_BANK;

  info->valid = 0; // will be set to valid if bank header has been read successfully

  char filepath[MAX_PATH];
  SEQ_FILE_B_BuildPath(filepath, session, bank);

#if DEBUG_VERBOSE_LEVEL >= 1
  DEBUG_MSG("[SEQ_FILE_B] Open bank file '%s'\n", filepath);
#endif

  s32 status;
  if( (status=FILE_ReadOpen((file_t*)&info->file, filepath)) < 0 ) {
#if DEBUG_VERBOSE_LEVEL >= 1
    DEBUG_MSG("[SEQ_FILE_B] failed to open file, status: %d\n", status);
#endif
    return status;
  }

  // read and check header
  // in order to avoid endianess issues, we have to read the sector bytewise!
  char file_type[10];
  if( (status=FILE_ReadBuffer((u8 *)file_type, 10)) < 0 ) {
#if DEBUG_VERBOSE_LEVEL >= 1
    DEBUG_MSG("[SEQ_FILE_B] failed to read header, status: %d\n", status);
#endif
    return status;
  }

  if( strncmp(file_type, "MBSEQV4_B", 10) != 0 ) {
#if DEBUG_VERBOSE_LEVEL >= 1
    file_type[9] = 0; // ensure that string is terminated
    DEBUG_MSG("[SEQ_FILE_B] wrong header type: %s\n", file_type);
#endif
    return SEQ_FILE_B_ERR_FORMAT;
  }

  status |= FILE_ReadBuffer((u8 *)info->header.name, 20);
  status |= FILE_ReadHWord((u16 *)&info->header.num_patterns);
  status |= FILE_ReadHWord((u16 *)&info->header.pattern_size);

  if( status < 0 ) {
#if DEBUG_VERBOSE_LEVEL >= 1
    DEBUG_MSG("[SEQ_FILE_B] file access error while reading header, status: %d\n", status);
#endif
    return SEQ_FILE_B_ERR_READ;
  }

  // close file (so that it can be re-opened)
  FILE_ReadClose((file_t*)&info->file);

  // bank is valid! :)
  info->valid = 1;

#if DEBUG_VERBOSE_LEVEL >= 1
  DEBUG_MSG("[SEQ_FILE_B] bank is valid! Number of Patterns: %d, Pattern Size: %d\n", info->header.num_patterns, info->header.pattern_size);
#endif

  // finally (re-)load cached pattern name - status of this function doesn't matter
  char dummy[21];
  SEQ_FILE_B_PatternPeekName(cached_bank, cached_pattern, 1, dummy); // non-cached!

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// reads a pattern from bank into given group
// returns < 0 on errors (error codes are documented in seq_file.h)
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
// V4 generator sub-block (Stage B) — shared by PatternRead and TrackRead.
// Reads the count byte + entries at the current file position and re-seeds
// dst_track's generator pool slots (the caller has already cleared them).
// Always leaves the file position at the end of the fixed-size sub-block
// regardless of count, so the per-track stride stays aligned. Pool-full /
// invalid entries are skipped with a debug note — the loop keeps streaming
// so position stays consistent.
/////////////////////////////////////////////////////////////////////////////
static s32 PatternGenBlockRead(u8 dst_track)
{
  u8 count = 0;
  s32 status = FILE_ReadByte(&count);
  if( status < 0 )
    return status;

  u32 entries_base = FILE_ReadGetCurrentPosition();

  if( count > SEQ_GENERATOR_PERSIST_SLOTS ) {
#if DEBUG_VERBOSE_LEVEL >= 1
    DEBUG_MSG("[SEQ_FILE_B] gen sub-block count %d invalid (max %d) - skipped\n", count, SEQ_GENERATOR_PERSIST_SLOTS);
#endif
    count = 0; // corrupt count: seed nothing, realign below
  }

  u8 e;
  for(e=0; e<count && status >= 0; ++e) {
    seq_generator_t g;
    memset(&g, 0, sizeof(g));

    u8 flag;
    status |= FILE_ReadByte(&g.instrument);
    status |= FILE_ReadByte(&g.par_layer);
    status |= FILE_ReadByte(&flag); g.engaged = flag ? 1 : 0;
    status |= FILE_ReadByte(&g.range_min);
    status |= FILE_ReadByte(&g.range_max);
    status |= FILE_ReadByte(&g.mutation_rate);
    status |= FILE_ReadByte(&g.mutation_depth);
    status |= FILE_ReadByte(&g.contour_shape);
    status |= FILE_ReadByte(&flag); g.anchor_valid = flag ? 1 : 0;
    status |= FILE_ReadBuffer(g.loop, SEQ_GENERATOR_LOOP_LEN);
    status |= FILE_ReadBuffer(g.locks, SEQ_GENERATOR_LOCKS_BYTES);
    status |= FILE_ReadBuffer(g.anchor, SEQ_GENERATOR_LOOP_LEN);
    status |= FILE_ReadBuffer(g.mult, SEQ_GENERATOR_MULT_BYTES);

    if( status >= 0 ) {
      s32 r = SEQ_GENERATOR_SlotSet(dst_track, g.instrument, &g);
      if( r < 0 ) {
#if DEBUG_VERBOSE_LEVEL >= 1
	DEBUG_MSG("[SEQ_FILE_B] gen re-seed refused on track %d instr %d (status %d)\n", dst_track+1, g.instrument, r);
#endif
      }
    }
  }

  // realign to the end of the fixed-size region (skips zero-filled entries)
  if( status >= 0 )
    status |= FILE_ReadSeek(entries_base + SEQ_GENERATOR_PERSIST_SLOTS*SEQ_FILE_B_TRK_EXT_GEN_ENTRY_SIZE);

  return status;
}


/////////////////////////////////////////////////////////////////////////////
// V4 generator sub-block write — serializes src_track's pool slots (up to the
// persist cap, instrument-ascending) at the current write position and
// zero-fills the unused entry slots so the region size is constant. Slots
// beyond the cap are dropped with a debug note (typical live use is one
// generator per track; the cap exists for drum tracks).
/////////////////////////////////////////////////////////////////////////////
static s32 PatternGenBlockWrite(u8 src_track)
{
  seq_generator_t *gens[SEQ_GENERATOR_PERSIST_SLOTS];
  u8 entries = 0;
  u8 dropped = 0;

  u8 instr;
  for(instr=0; instr<SEQ_GENERATOR_INSTRUMENTS; ++instr) {
    seq_generator_t *g = SEQ_GENERATOR_Get(src_track, instr);
    if( g == NULL )
      continue;
    if( entries < SEQ_GENERATOR_PERSIST_SLOTS )
      gens[entries++] = g;
    else
      ++dropped;
  }

  if( dropped ) {
#if DEBUG_VERBOSE_LEVEL >= 1
    DEBUG_MSG("[SEQ_FILE_B] track %d has %d generators beyond the persist cap (%d) - not saved\n", src_track+1, dropped, SEQ_GENERATOR_PERSIST_SLOTS);
#endif
  }

  s32 status = FILE_WriteByte(entries);

  u8 e;
  for(e=0; e<entries; ++e) {
    seq_generator_t *g = gens[e];
    status |= FILE_WriteByte(g->instrument);
    status |= FILE_WriteByte(g->par_layer);
    status |= FILE_WriteByte(g->engaged ? 1 : 0);
    status |= FILE_WriteByte(g->range_min);
    status |= FILE_WriteByte(g->range_max);
    status |= FILE_WriteByte(g->mutation_rate);
    status |= FILE_WriteByte(g->mutation_depth);
    status |= FILE_WriteByte(g->contour_shape);
    status |= FILE_WriteByte(g->anchor_valid ? 1 : 0);
    status |= FILE_WriteBuffer((u8 *)g->loop, SEQ_GENERATOR_LOOP_LEN);
    status |= FILE_WriteBuffer((u8 *)g->locks, SEQ_GENERATOR_LOCKS_BYTES);
    status |= FILE_WriteBuffer((u8 *)g->anchor, SEQ_GENERATOR_LOOP_LEN);
    status |= FILE_WriteBuffer((u8 *)g->mult, SEQ_GENERATOR_MULT_BYTES);
  }

  int pad = (SEQ_GENERATOR_PERSIST_SLOTS - entries) * SEQ_FILE_B_TRK_EXT_GEN_ENTRY_SIZE;
  while( pad-- > 0 )
    status |= FILE_WriteByte(0x00);

  return status;
}


s32 SEQ_FILE_B_PatternRead(u8 bank, u8 pattern, u8 target_group, u16 remix_map)
{
  seq_file_b_info_t *info = SEQ_FILE_B_InfoPtr(bank);
  if( info == NULL )
    return SEQ_FILE_B_ERR_INVALID_BANK;

  if( target_group >= SEQ_CORE_NUM_GROUPS )
    return SEQ_FILE_B_ERR_INVALID_GROUP;

  if( !info->valid )
    return SEQ_FILE_B_ERR_NO_FILE;

  if( pattern >= info->header.num_patterns )
    return SEQ_FILE_B_ERR_INVALID_PATTERN;

  // re-open file
  if( FILE_ReadReOpen((file_t*)&info->file) < 0 )
    return -1; // file cannot be re-opened

  // change to file position
  s32 status;
  u32 offset = 10 + sizeof(seq_file_b_header_t) + pattern * info->header.pattern_size;
  if( (status=FILE_ReadSeek(offset)) < 0 ) {
#if DEBUG_VERBOSE_LEVEL >= 1
    DEBUG_MSG("[SEQ_FILE_B] failed to change pattern offset in file, status: %d\n", status);
#endif
    // close file (so that it can be re-opened)
    FILE_ReadClose((file_t*)&info->file);
    return SEQ_FILE_B_ERR_READ;
  }

  status |= FILE_ReadBuffer((u8 *)seq_pattern_name[target_group], 20);
  seq_pattern_name[target_group][20] = 0;

  u8 num_tracks;
  status |= FILE_ReadByte(&num_tracks);

  u8 mixer_map;
  status |= FILE_ReadByte(&mixer_map);

  u8 sysex_setup;
  status |= FILE_ReadByte(&sysex_setup);

  u8 reserved;
  status |= FILE_ReadByte(&reserved);

#if DEBUG_VERBOSE_LEVEL >= 1
  DEBUG_MSG("[SEQ_FILE_B] read pattern B%d:P%d '%s', %d tracks\n", bank+1, pattern, seq_pattern_name[target_group], num_tracks);
#endif

  // reduce number of tracks if required
  if( num_tracks > SEQ_CORE_NUM_TRACKS_PER_GROUP )
    num_tracks = SEQ_CORE_NUM_TRACKS_PER_GROUP;

  u8 track_i;
  u8 track = target_group * SEQ_CORE_NUM_TRACKS_PER_GROUP;
  for(track_i=0; track_i<num_tracks; ++track_i, ++track) {
		
    // if we got the track bit setup inside our remix_map, them do not change him, let it be mixed down
    if ( ((1 << track) | remix_map) == remix_map ) {
      // Mixed down! no need to change the track pattern
      // but we need to state our file pointer... jump to the next track data

DEBUG_MSG("Skipping Track %d\n", track);
      u8 dummy_name[80];
      status |= FILE_ReadBuffer(dummy_name, 80); // dummy! don't take over track name

      u8 num_p_instruments;
      status |= FILE_ReadByte(&num_p_instruments);

      u8 num_t_instruments;
      status |= FILE_ReadByte(&num_t_instruments);

      u8 num_p_layers;
      status |= FILE_ReadByte(&num_p_layers);

      u8 num_t_layers;
      status |= FILE_ReadByte(&num_t_layers);

      u16 p_layer_size;
      status |= FILE_ReadHWord(&p_layer_size);

      u16 t_layer_size;
      status |= FILE_ReadHWord(&t_layer_size);

      // skip CC and Par/Trg layer
      u32 par_size = num_p_instruments * num_p_layers * p_layer_size;
      u32 trg_size = num_t_instruments * num_t_layers * t_layer_size;
      u32 new_pos = FILE_ReadGetCurrentPosition() + 128 + par_size + trg_size;
 DEBUG_MSG("Pos change: %d -> %d\n", FILE_ReadGetCurrentPosition(), new_pos);
      if( (status=FILE_ReadSeek(new_pos)) < 0 ) {
#if DEBUG_VERBOSE_LEVEL >= 1
	DEBUG_MSG("[SEQ_FILE_B] failed to change pattern offset in file, status: %d\n", status);
#endif
	// close file (so that it can be re-opened)
	FILE_ReadClose((file_t*)&info->file);
	return SEQ_FILE_B_ERR_READ;
      }
 DEBUG_MSG("New pos: %d\n", FILE_ReadGetCurrentPosition());

    } else {
			
      status |= FILE_ReadBuffer((u8 *)seq_core_trk[track].name, 80);
      seq_core_trk[track].name[80] = 0;

      u8 num_p_instruments;
      status |= FILE_ReadByte(&num_p_instruments);

      u8 num_t_instruments;
      status |= FILE_ReadByte(&num_t_instruments);

      u8 num_p_layers;
      status |= FILE_ReadByte(&num_p_layers);

      u8 num_t_layers;
      status |= FILE_ReadByte(&num_t_layers);

      u16 p_layer_size;
      status |= FILE_ReadHWord(&p_layer_size);

      u16 t_layer_size;
      status |= FILE_ReadHWord(&t_layer_size);

      u8 cc_buffer[128];
      status |= FILE_ReadBuffer(cc_buffer, 128);
    
      // before changing CCs: we should stop here on error if read failed
      if( status < 0 ) {
#if DEBUG_VERBOSE_LEVEL >= 2
	DEBUG_MSG("[SEQ_FILE_B] read track #%d (-> %d) failed due to file access error, status: %d\n", track+1, track+1, status);
#endif
	break;
      }

#if DEBUG_VERBOSE_LEVEL >= 2
      DEBUG_MSG("[SEQ_FILE_B] read track #%d (-> %d) '%s'\n", track+1, track+1, seq_core_trk[track].name);
      DEBUG_MSG("[SEQ_FILE_B] P:%d,T:%d instruments P:%d,T:%d layers P:%d,T:%d steps\n", 
		num_p_instruments, num_t_instruments,
		num_p_layers, num_t_layers,
		p_layer_size, 8*t_layer_size);
#endif

      // reading CCs
      u8 cc;
      for(cc=0; cc<128; ++cc)
	SEQ_CC_Set(track, cc, cc_buffer[cc]);

      // partitionate parameter layer and clear all steps
      SEQ_PAR_TrackInit(track, p_layer_size, num_p_layers, num_p_instruments);

      // reading Parameter layers
      u32 par_size = num_p_instruments * num_p_layers * p_layer_size;
      u32 par_size_taken = (par_size > SEQ_PAR_MAX_BYTES) ? SEQ_PAR_MAX_BYTES : par_size;
      if( par_size_taken )
	FILE_ReadBuffer((u8 *)&seq_par_layer_value[track], par_size_taken);

      // read remaining bytes into dummy buffer
      while( par_size > par_size_taken ) {
	u8 dummy;
	FILE_ReadByte(&dummy);
	++par_size_taken;
      }

      // partitionate trigger layer and clear all steps
      SEQ_TRG_TrackInit(track, t_layer_size*8, num_t_layers, num_t_instruments);

      // reading Trigger layers
      u32 trg_size = num_t_instruments * num_t_layers * t_layer_size;
      u32 trg_size_taken = (trg_size > SEQ_TRG_MAX_BYTES) ? SEQ_TRG_MAX_BYTES : trg_size;
      if( trg_size_taken )
	FILE_ReadBuffer((u8 *)&seq_trg_layer_value[track], trg_size_taken);

      // read remaining bytes into dummy buffer
      while( trg_size > trg_size_taken ) {
	u8 dummy;
	FILE_ReadByte(&dummy);
	++trg_size_taken;
      }

      // re-arm render-cache dirty: the SEQ_*_TrackInit calls above already
      // marked the track dirty, but a tick concurrent with the FILE_ReadBuffer
      // could have rendered the just-zeroed source into output. Mark dirty
      // *after* the bulk load so the next tick refreshes the mirror.
      SEQ_CORE_RenderDirtySet(track);

      // finally update CC links again, because some of them depend on SEQ_PAR_NumLayersGet()!!!
      SEQ_CC_LinkUpdate(track);

      // Stage B — gen state is slot content: the track's content was just
      // replaced wholesale, so its generators go with it. A V4 ext block
      // below re-seeds them from the record; a V1-V3/absent block means the
      // slot has no organism and the track comes back generator-less.
      // (Remix-skipped tracks keep their generators, same as their content.)
      SEQ_GENERATOR_TrackClear(track);

    }
  }

  // optional per-track extension blocks. peek the first tag byte to detect
  // format; if absent (old file -> zero-fill where the tag would be) leave
  // ext CCs / anchors at whatever the runtime already had.
  //
  // tag 0x02 (v2): 22 ext CC bytes (0x80..0x95) + 64 anchor bytes
  // tag 0x01 (v1): 64 anchor bytes only (brief dev format - ext CCs default)
  //
  // all tracks within a pattern share the same tag (we never write a mix);
  // the per-track skip path uses the same size for every track.
  if( status >= 0 ) {
    u8 first_tag = 0;
    u32 peek_pos = FILE_ReadGetCurrentPosition();
    if( FILE_ReadByte(&first_tag) >= 0 ) {
      FILE_ReadSeek(peek_pos); // rewind regardless of what we saw

      u16 per_track_ext_size = 0;
      if( first_tag == SEQ_FILE_B_TRK_EXT_TAG_V4 )
	per_track_ext_size = SEQ_FILE_B_TRK_EXT_V4_SIZE;
      else if( first_tag == SEQ_FILE_B_TRK_EXT_TAG_V3 )
	per_track_ext_size = SEQ_FILE_B_TRK_EXT_V3_SIZE;
      else if( first_tag == SEQ_FILE_B_TRK_EXT_TAG_V2 )
	per_track_ext_size = SEQ_FILE_B_TRK_EXT_V2_SIZE;
      else if( first_tag == SEQ_FILE_B_TRK_EXT_TAG_V1 )
	per_track_ext_size = SEQ_FILE_B_TRK_EXT_V1_SIZE;

      if( per_track_ext_size ) {
	track = target_group * SEQ_CORE_NUM_TRACKS_PER_GROUP;
	for(track_i=0; track_i<num_tracks; ++track_i, ++track) {
	  if( ((1 << track) | remix_map) == remix_map ) {
	    // skipped track: keep in-RAM ext CCs / anchors, advance past
	    u32 new_pos = FILE_ReadGetCurrentPosition() + per_track_ext_size;
	    status |= FILE_ReadSeek(new_pos);
	  } else {
	    u8 tag = 0;
	    status |= FILE_ReadByte(&tag);

	    if( tag == SEQ_FILE_B_TRK_EXT_TAG_V4 || tag == SEQ_FILE_B_TRK_EXT_TAG_V3 ) {
	      u8 ext_cc_buffer[SEQ_FILE_B_TRK_EXT_CC_COUNT];   // 32 (0x80..0x9f)
	      status |= FILE_ReadBuffer(ext_cc_buffer, SEQ_FILE_B_TRK_EXT_CC_COUNT);
	      u8 i;
	      for(i=0; i<SEQ_FILE_B_TRK_EXT_CC_COUNT; ++i)
		SEQ_CC_Set(track, SEQ_FILE_B_TRK_EXT_CC_FIRST + i, ext_cc_buffer[i]);
	      status |= FILE_ReadBuffer((u8 *)seq_cc_trk[track].robotize_bar_anchors,
					SEQ_FILE_B_TRK_EXT_ANCHORS_SIZE);
	      if( tag == SEQ_FILE_B_TRK_EXT_TAG_V4 )
		status |= PatternGenBlockRead(track);
	    } else if( tag == SEQ_FILE_B_TRK_EXT_TAG_V2 ) {
	      u8 ext_cc_buffer[SEQ_FILE_B_TRK_EXT_CC_COUNT_V2];  // 22 frozen (0x80..0x95)
	      status |= FILE_ReadBuffer(ext_cc_buffer, SEQ_FILE_B_TRK_EXT_CC_COUNT_V2);
	      u8 i;
	      for(i=0; i<SEQ_FILE_B_TRK_EXT_CC_COUNT_V2; ++i)
		SEQ_CC_Set(track, SEQ_FILE_B_TRK_EXT_CC_FIRST + i, ext_cc_buffer[i]);
	      status |= FILE_ReadBuffer((u8 *)seq_cc_trk[track].robotize_bar_anchors,
					SEQ_FILE_B_TRK_EXT_ANCHORS_SIZE);
	    } else if( tag == SEQ_FILE_B_TRK_EXT_TAG_V1 ) {
	      status |= FILE_ReadBuffer((u8 *)seq_cc_trk[track].robotize_bar_anchors,
					SEQ_FILE_B_TRK_EXT_ANCHORS_SIZE);
	    } else {
	      // mid-pattern format break - stop to avoid misalignment
	      break;
	    }
	  }
	}
      }
    }
  }

  // close file (so that it can be re-opened)
  FILE_ReadClose((file_t*)&info->file);

  if( status < 0 ) {
#if DEBUG_VERBOSE_LEVEL >= 1
    DEBUG_MSG("[SEQ_FILE_B] error while reading file, status: %d\n", status);
#endif
    return SEQ_FILE_B_ERR_READ;
  }

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Fork: reads ONE track section of a pattern slot into an arbitrary live
// track — the missing grain cell (track-save/group-save/group-load exist;
// this is track-load). Fully general: any bank x pattern x section -> any of
// the 16 live tracks. The slot's other sections and the rest of the file are
// untouched; only dst_track's live state changes.
//
// Adapted from SEQ_FILE_B_PatternRead: the remix-skip arm walks preceding
// sections (per-track geometry, no fixed stride), the load arm streams the
// wanted section, the ext-block arm dispatches the section's V1/V2/V3 tag.
// Unlike PatternRead (which writes the track name into live state before its
// status check), the fixed section header is read into locals and checked
// BEFORE the first live write — every pre-write failure (bad bank/pattern/
// section, slot storing fewer tracks, header read error) leaves the live
// track untouched. Past that point a failed bulk read can leave the track
// half-written; callers arm the track undo first (SEQ_CORE_LoadTrackFromSlot).
//
// Does NOT touch seq_pattern[]/seq_pattern_name[] (a pull is a transfusion,
// not a pattern switch) and runs no post-load side effects — the census fan
// (sustain cancel, PC/bank send, latch reset, unmute, bar-align) lives in the
// calling verb.
//
// returns < 0 on errors (error codes are documented in seq_file.h)
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_FILE_B_TrackRead(u8 bank, u8 pattern, u8 slot_track, u8 dst_track)
{
  seq_file_b_info_t *info = SEQ_FILE_B_InfoPtr(bank);
  if( info == NULL )
    return SEQ_FILE_B_ERR_INVALID_BANK;

  if( slot_track >= SEQ_CORE_NUM_TRACKS_PER_GROUP )
    return SEQ_FILE_B_ERR_INVALID_TRACK;

  if( dst_track >= SEQ_CORE_NUM_TRACKS )
    return SEQ_FILE_B_ERR_INVALID_TRACK;

  if( !info->valid )
    return SEQ_FILE_B_ERR_NO_FILE;

  if( pattern >= info->header.num_patterns )
    return SEQ_FILE_B_ERR_INVALID_PATTERN;

  // re-open file
  if( FILE_ReadReOpen((file_t*)&info->file) < 0 )
    return -1; // file cannot be re-opened

  // change to file position
  s32 status;
  u32 offset = 10 + sizeof(seq_file_b_header_t) + pattern * info->header.pattern_size;
  if( (status=FILE_ReadSeek(offset)) < 0 ) {
    FILE_ReadClose((file_t*)&info->file);
    return SEQ_FILE_B_ERR_READ;
  }

  // pattern header: skip the pattern name (the slot name describes the whole
  // group slot; the pulled track carries its own 80-char section name)
  u8 dummy_pattern_name[20];
  status |= FILE_ReadBuffer(dummy_pattern_name, 20);

  u8 num_tracks;
  status |= FILE_ReadByte(&num_tracks);

  u8 dummy_byte;
  status |= FILE_ReadByte(&dummy_byte); // mixer_map
  status |= FILE_ReadByte(&dummy_byte); // sysex_setup
  status |= FILE_ReadByte(&dummy_byte); // reserved1

  if( num_tracks > SEQ_CORE_NUM_TRACKS_PER_GROUP )
    num_tracks = SEQ_CORE_NUM_TRACKS_PER_GROUP;

  if( status < 0 ) {
    FILE_ReadClose((file_t*)&info->file);
    return SEQ_FILE_B_ERR_READ;
  }

  // refuse a section index the slot doesn't store. NOTE: this is NOT a
  // reliable unwritten-slot guard — SEQ_FILE_B_Create's slot zero-fill is
  // #if 0'd, so a header-only bank refuses via ERR_READ (short header read)
  // instead, and a sparsely-written bank's never-written slots hold undefined
  // FatFs gap data that can read as nonzero num_tracks and load garbage.
  // Accepted gap: stock SEQ_FILE_Format writes all 64 slots; only testctrl
  // bank_create + selective pattern_save creates sparse banks.
  if( slot_track >= num_tracks ) {
    FILE_ReadClose((file_t*)&info->file);
    return SEQ_FILE_B_ERR_INVALID_TRACK;
  }

  // walk the sections before slot_track: each section's size depends on its
  // own stored geometry, so locating section N means reading the 6 geometry
  // fields of every preceding section and seeking past its data
  u8 track_i;
  for(track_i=0; track_i<slot_track; ++track_i) {
    u8 dummy_name[80];
    status |= FILE_ReadBuffer(dummy_name, 80);

    u8 skip_p_instruments, skip_t_instruments, skip_p_layers, skip_t_layers;
    u16 skip_p_size, skip_t_size;
    status |= FILE_ReadByte(&skip_p_instruments);
    status |= FILE_ReadByte(&skip_t_instruments);
    status |= FILE_ReadByte(&skip_p_layers);
    status |= FILE_ReadByte(&skip_t_layers);
    status |= FILE_ReadHWord(&skip_p_size);
    status |= FILE_ReadHWord(&skip_t_size);

    u32 skip_par = skip_p_instruments * skip_p_layers * skip_p_size;
    u32 skip_trg = skip_t_instruments * skip_t_layers * skip_t_size;
    u32 new_pos = FILE_ReadGetCurrentPosition() + 128 + skip_par + skip_trg;
    if( status < 0 || (status=FILE_ReadSeek(new_pos)) < 0 ) {
      FILE_ReadClose((file_t*)&info->file);
      return SEQ_FILE_B_ERR_READ;
    }
  }

  // section slot_track: fixed part into locals, status-checked before the
  // first live write
  char name_buffer[80];
  status |= FILE_ReadBuffer((u8 *)name_buffer, 80);

  u8 num_p_instruments, num_t_instruments, num_p_layers, num_t_layers;
  u16 p_layer_size, t_layer_size;
  status |= FILE_ReadByte(&num_p_instruments);
  status |= FILE_ReadByte(&num_t_instruments);
  status |= FILE_ReadByte(&num_p_layers);
  status |= FILE_ReadByte(&num_t_layers);
  status |= FILE_ReadHWord(&p_layer_size);
  status |= FILE_ReadHWord(&t_layer_size);

  u8 cc_buffer[128];
  status |= FILE_ReadBuffer(cc_buffer, 128);

  if( status < 0 ) {
    FILE_ReadClose((file_t*)&info->file);
    return SEQ_FILE_B_ERR_READ;
  }

  // ---- first live write below this line ----

  memcpy(seq_core_trk[dst_track].name, name_buffer, 80);
  seq_core_trk[dst_track].name[80] = 0;

  u8 cc;
  for(cc=0; cc<128; ++cc)
    SEQ_CC_Set(dst_track, cc, cc_buffer[cc]);

  // partitionate parameter layer and clear all steps
  SEQ_PAR_TrackInit(dst_track, p_layer_size, num_p_layers, num_p_instruments);

  // reading Parameter layers. Unlike PatternRead, the bulk reads stay
  // status-bearing: the caller branches on the return code (a swallowed
  // failure here would report success for a half-written track — and when
  // slot_track is the last stored section, no later read would catch it).
  u32 par_size = num_p_instruments * num_p_layers * p_layer_size;
  u32 par_size_taken = (par_size > SEQ_PAR_MAX_BYTES) ? SEQ_PAR_MAX_BYTES : par_size;
  if( par_size_taken )
    status |= FILE_ReadBuffer((u8 *)&seq_par_layer_value[dst_track], par_size_taken);

  // read remaining bytes into dummy buffer
  while( par_size > par_size_taken ) {
    u8 dummy;
    status |= FILE_ReadByte(&dummy);
    ++par_size_taken;
  }

  // partitionate trigger layer and clear all steps
  SEQ_TRG_TrackInit(dst_track, t_layer_size*8, num_t_layers, num_t_instruments);

  // reading Trigger layers
  u32 trg_size = num_t_instruments * num_t_layers * t_layer_size;
  u32 trg_size_taken = (trg_size > SEQ_TRG_MAX_BYTES) ? SEQ_TRG_MAX_BYTES : trg_size;
  if( trg_size_taken )
    status |= FILE_ReadBuffer((u8 *)&seq_trg_layer_value[dst_track], trg_size_taken);

  // read remaining bytes into dummy buffer
  while( trg_size > trg_size_taken ) {
    u8 dummy;
    status |= FILE_ReadByte(&dummy);
    ++trg_size_taken;
  }

  // re-arm render-cache dirty *after* the bulk load (same concurrent-tick
  // race note as in SEQ_FILE_B_PatternRead)
  SEQ_CORE_RenderDirtySet(dst_track);

  // finally update CC links again, because some of them depend on SEQ_PAR_NumLayersGet()!!!
  SEQ_CC_LinkUpdate(dst_track);

  // a mid-bulk failure means the track is half-written — report it (the
  // calling verb restores the armed undo victim); skip the ext phase
  if( status < 0 ) {
    FILE_ReadClose((file_t*)&info->file);
    return SEQ_FILE_B_ERR_READ;
  }

  // Stage B — the pull replaces dst_track's content wholesale, so its
  // generators go with it; the V4 ext arm below re-seeds from the section's
  // record (the pull carries the organism). Cleared here, after the mandatory
  // section committed: every pre-write refusal above leaves the live track —
  // generators included — untouched, and an ext-phase degrade ("loaded
  // without ext") correctly yields a generator-less track.
  SEQ_GENERATOR_TrackClear(dst_track);

  // ext block for slot_track: walk the remaining sections to the ext base,
  // peek the shared tag (all tracks within a pattern carry the same tag),
  // then index slot_track's block directly. Absent/unknown tag -> old file,
  // leave the in-RAM ext CCs / anchors alone (same semantic as PatternRead).
  //
  // The ext phase is OPTIONAL and uses its own status: the mandatory section
  // is fully committed by now, so an I/O failure past this point degrades to
  // "loaded without ext" (success) instead of misreporting a refusal — the
  // caller must still run its post-load fan on the swapped content.
  s32 ext_status = 0;
  for(track_i=slot_track+1; track_i<num_tracks && ext_status >= 0; ++track_i) {
    u8 dummy_name[80];
    ext_status |= FILE_ReadBuffer(dummy_name, 80);

    u8 skip_p_instruments, skip_t_instruments, skip_p_layers, skip_t_layers;
    u16 skip_p_size, skip_t_size;
    ext_status |= FILE_ReadByte(&skip_p_instruments);
    ext_status |= FILE_ReadByte(&skip_t_instruments);
    ext_status |= FILE_ReadByte(&skip_p_layers);
    ext_status |= FILE_ReadByte(&skip_t_layers);
    ext_status |= FILE_ReadHWord(&skip_p_size);
    ext_status |= FILE_ReadHWord(&skip_t_size);

    u32 skip_par = skip_p_instruments * skip_p_layers * skip_p_size;
    u32 skip_trg = skip_t_instruments * skip_t_layers * skip_t_size;
    if( ext_status >= 0 )
      ext_status |= FILE_ReadSeek(FILE_ReadGetCurrentPosition() + 128 + skip_par + skip_trg);
  }

  if( ext_status >= 0 ) {
    u32 ext_base = FILE_ReadGetCurrentPosition();
    u8 first_tag = 0;
    if( FILE_ReadByte(&first_tag) >= 0 ) {
      u16 per_track_ext_size = 0;
      if( first_tag == SEQ_FILE_B_TRK_EXT_TAG_V4 )
	per_track_ext_size = SEQ_FILE_B_TRK_EXT_V4_SIZE;
      else if( first_tag == SEQ_FILE_B_TRK_EXT_TAG_V3 )
	per_track_ext_size = SEQ_FILE_B_TRK_EXT_V3_SIZE;
      else if( first_tag == SEQ_FILE_B_TRK_EXT_TAG_V2 )
	per_track_ext_size = SEQ_FILE_B_TRK_EXT_V2_SIZE;
      else if( first_tag == SEQ_FILE_B_TRK_EXT_TAG_V1 )
	per_track_ext_size = SEQ_FILE_B_TRK_EXT_V1_SIZE;

      if( per_track_ext_size &&
	  FILE_ReadSeek(ext_base + slot_track * per_track_ext_size) >= 0 ) {
	u8 tag = 0;
	ext_status |= FILE_ReadByte(&tag);

	// the stride above came from first_tag, so only a matching tag may be
	// parsed at the indexed position — a valid-but-different tag means a
	// corrupt/foreign file (mixed tags are never written): leave the
	// in-RAM ext values alone
	if( ext_status >= 0 && tag == first_tag ) {
	  if( tag == SEQ_FILE_B_TRK_EXT_TAG_V4 || tag == SEQ_FILE_B_TRK_EXT_TAG_V3 ) {
	    u8 ext_cc_buffer[SEQ_FILE_B_TRK_EXT_CC_COUNT];   // 32 (0x80..0x9f)
	    ext_status |= FILE_ReadBuffer(ext_cc_buffer, SEQ_FILE_B_TRK_EXT_CC_COUNT);
	    if( ext_status >= 0 ) {
	      u8 i;
	      for(i=0; i<SEQ_FILE_B_TRK_EXT_CC_COUNT; ++i)
		SEQ_CC_Set(dst_track, SEQ_FILE_B_TRK_EXT_CC_FIRST + i, ext_cc_buffer[i]);
	      ext_status |= FILE_ReadBuffer((u8 *)seq_cc_trk[dst_track].robotize_bar_anchors,
					    SEQ_FILE_B_TRK_EXT_ANCHORS_SIZE);
	      if( ext_status >= 0 && tag == SEQ_FILE_B_TRK_EXT_TAG_V4 )
		ext_status |= PatternGenBlockRead(dst_track);
	    }
	  } else if( tag == SEQ_FILE_B_TRK_EXT_TAG_V2 ) {
	    u8 ext_cc_buffer[SEQ_FILE_B_TRK_EXT_CC_COUNT_V2];  // 22 frozen (0x80..0x95)
	    ext_status |= FILE_ReadBuffer(ext_cc_buffer, SEQ_FILE_B_TRK_EXT_CC_COUNT_V2);
	    if( ext_status >= 0 ) {
	      u8 i;
	      for(i=0; i<SEQ_FILE_B_TRK_EXT_CC_COUNT_V2; ++i)
		SEQ_CC_Set(dst_track, SEQ_FILE_B_TRK_EXT_CC_FIRST + i, ext_cc_buffer[i]);
	      ext_status |= FILE_ReadBuffer((u8 *)seq_cc_trk[dst_track].robotize_bar_anchors,
					    SEQ_FILE_B_TRK_EXT_ANCHORS_SIZE);
	    }
	  } else if( tag == SEQ_FILE_B_TRK_EXT_TAG_V1 ) {
	    ext_status |= FILE_ReadBuffer((u8 *)seq_cc_trk[dst_track].robotize_bar_anchors,
					  SEQ_FILE_B_TRK_EXT_ANCHORS_SIZE);
	  }
	}
      }
    }
  }

  // close file (so that it can be re-opened)
  FILE_ReadClose((file_t*)&info->file);

  return 0; // no error (ext_status failures degraded to "loaded without ext")
}


/////////////////////////////////////////////////////////////////////////////
// writes a pattern of a given group into bank
// returns < 0 on errors (error codes are documented in seq_file.h)
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_FILE_B_PatternWrite(char *session, u8 bank, u8 pattern, u8 source_group, u8 rename_if_empty_name)
{
  seq_file_b_info_t *info = SEQ_FILE_B_InfoPtr(bank);
  if( info == NULL )
    return SEQ_FILE_B_ERR_INVALID_BANK;

  if( source_group >= SEQ_CORE_NUM_GROUPS )
    return SEQ_FILE_B_ERR_INVALID_GROUP;

  if( !info->valid )
    return SEQ_FILE_B_ERR_FORMAT;

  if( pattern >= info->header.num_patterns )
    return SEQ_FILE_B_ERR_INVALID_PATTERN;


  // TODO: before writing into pattern slot, we should check if it already exists, and then
  // compare layer parameters with given constraints available in following defines/variables:
  u8 num_tracks = SEQ_CORE_NUM_TRACKS_PER_GROUP;

  // ok, we should at least check, if the resulting size is within the given range.
  // compute base size (everything that has always been written) separately from
  // the per-track ext blocks, so we can skip ext when the slot was allocated by
  // older firmware that didn't reserve room for it. degrading skips anchor
  // persistence for that pattern but avoids overflowing into the next slot.
  u16 base_pattern_size = sizeof(seq_file_b_pattern_t);

  u8 track = source_group * SEQ_CORE_NUM_TRACKS_PER_GROUP;
  u8 track_i;
  for(track_i=0; track_i<num_tracks; ++track_i, ++track) {
    u8 num_p_instruments = SEQ_PAR_NumInstrumentsGet(track);
    u8 num_p_layers = SEQ_PAR_NumLayersGet(track);
    u16 p_layer_size = SEQ_PAR_NumStepsGet(track);
    u8 num_t_instruments = SEQ_TRG_NumInstrumentsGet(track);
    u8 num_t_layers = SEQ_TRG_NumLayersGet(track);
    u16 t_layer_size = SEQ_TRG_NumStepsGet(track)/8;

    base_pattern_size += sizeof(seq_file_b_track_t) +
      num_p_instruments*num_p_layers*p_layer_size +
      num_t_instruments*num_t_layers*t_layer_size;
  }

  // Ext-block fit arbitration, per record: V4 (gen state) if the slot has
  // room, else degrade to V3 (today's payload — gen state won't persist),
  // else no ext at all (pre-V2 slots). All-or-nothing per level so a record
  // never carries mixed tags.
  u8 ext_tag = 0;
  u16 ext_pattern_size = 0;
  if( base_pattern_size + (u32)num_tracks * SEQ_FILE_B_TRK_EXT_V4_SIZE <= info->header.pattern_size ) {
    ext_tag = SEQ_FILE_B_TRK_EXT_TAG_V4;
    ext_pattern_size = num_tracks * SEQ_FILE_B_TRK_EXT_V4_SIZE;
  } else if( base_pattern_size + (u32)num_tracks * SEQ_FILE_B_TRK_EXT_V3_SIZE <= info->header.pattern_size ) {
    ext_tag = SEQ_FILE_B_TRK_EXT_TAG_V3;
    ext_pattern_size = num_tracks * SEQ_FILE_B_TRK_EXT_V3_SIZE;
  }
  u8 write_ext = (ext_tag != 0);
  u16 expected_pattern_size = base_pattern_size + ext_pattern_size;

  if( expected_pattern_size > info->header.pattern_size ) {
#if DEBUG_VERBOSE_LEVEL >= 1
    DEBUG_MSG("[SEQ_FILE_B] Resulting pattern is too large for slot in bank (is: %d, max: %d)\n", 
	   expected_pattern_size, info->header.pattern_size);
    return SEQ_FILE_B_ERR_P_TOO_LARGE;
#endif
  }

  char filepath[MAX_PATH];
  SEQ_FILE_B_BuildPath(filepath, session, bank);

#if DEBUG_VERBOSE_LEVEL >= 1
  DEBUG_MSG("[SEQ_FILE_B] Open bank file '%s' for writing\n", filepath);
#endif

  s32 status = 0;
  if( (status=FILE_WriteOpen(filepath, 0)) < 0 ) {
#if DEBUG_VERBOSE_LEVEL >= 1
    DEBUG_MSG("[SEQ_FILE_B] Failed to open file, status: %d\n", status);
#endif
    FILE_WriteClose(); // important to free memory given by malloc
    return status;
  }

  // change to file position
  u32 offset = 10 + sizeof(seq_file_b_header_t) + pattern * info->header.pattern_size;
  if( (status=FILE_WriteSeek(offset)) < 0 ) {
#if DEBUG_VERBOSE_LEVEL >= 1
    DEBUG_MSG("[SEQ_FILE_B] failed to change pattern offset in file, status: %d\n", status);
#endif
    FILE_WriteClose(); // important to free memory given by malloc
    return status;
  }

  // rename pattern if name is empty
  if( rename_if_empty_name ) {
    int i;
    u8 found_char = 0;
    u8 *label = (u8 *)&seq_pattern_name[source_group][5];
    for(i=0; i<15; ++i)
      if( label[i] != ' ' ) {
	found_char = 1;
	break;
      }

    if( !found_char )
      memcpy(label, "Unnamed        ", 15);
  }

  // write pattern name w/o zero terminator
  status |= FILE_WriteBuffer((u8 *)seq_pattern_name[source_group], 20);

#if DEBUG_VERBOSE_LEVEL >= 2
  DEBUG_MSG("[SEQ_FILE_B] writing pattern '%s'...\n", seq_pattern_name[source_group]);
#endif

  // write number of tracks
  status |= FILE_WriteByte(num_tracks);

  // write link to mixer map
  status |= FILE_WriteByte(0x00); // off

  // write link to SysEx setup
  status |= FILE_WriteByte(0x00); // off

  // reserved
  status |= FILE_WriteByte(0x00);

  // writing tracks
  track = source_group * SEQ_CORE_NUM_TRACKS_PER_GROUP;
  for(track_i=0; track_i<num_tracks; ++track_i, ++track) {
    u8 num_p_instruments = SEQ_PAR_NumInstrumentsGet(track);
    u8 num_p_layers = SEQ_PAR_NumLayersGet(track);
    u16 p_layer_size = SEQ_PAR_NumStepsGet(track);
    u8 num_t_instruments = SEQ_TRG_NumInstrumentsGet(track);
    u8 num_t_layers = SEQ_TRG_NumLayersGet(track);
    u16 t_layer_size = SEQ_TRG_NumStepsGet(track)/8;

    // write track name w/o zero terminator
    status |= FILE_WriteBuffer((u8 *)seq_core_trk[track].name, 80);

    // write number of parameter instruments
    status |= FILE_WriteByte(num_p_instruments);

    // write number of trigger instruments
    status |= FILE_WriteByte(num_t_instruments);

    // write number of parameter layers
    status |= FILE_WriteByte(num_p_layers);

    // write number of trigger layers
    status |= FILE_WriteByte(num_t_layers);

    // write size of a single parameter layer
    status |= FILE_WriteHWord(p_layer_size);

    // write size of a single trigger layer
    status |= FILE_WriteHWord(t_layer_size);

    // write 128 CCs
    u8 cc;
    for(cc=0; cc<128; ++cc) {
      s32 cc_value = SEQ_CC_Get(track, cc);
      if( cc_value < 0 ) // set CC value to 0 if it doesn't exist (reserved CCs)
	cc_value = 0;
      status |= FILE_WriteByte(cc_value);
    }

    // write parameter layers
    status |= FILE_WriteBuffer((u8 *)&seq_par_layer_value[track], num_p_instruments*num_p_layers*p_layer_size);

    // write trigger layers
    status |= FILE_WriteBuffer((u8 *)&seq_trg_layer_value[track], num_t_instruments*num_t_layers*t_layer_size);
  }

  // per-track extension blocks (see SEQ_FILE_B_TRK_EXT_* defines).
  // appended after all tracks so older firmware reading this file
  // sees the bytes as part of its trailing zero-fill. skipped when the
  // existing slot was sized by older firmware and can't fit the extra
  // bytes - the ext CCs / anchors won't persist for that pattern but no
  // overflow into the next slot.
  if( write_ext ) {
    track = source_group * SEQ_CORE_NUM_TRACKS_PER_GROUP;
    for(track_i=0; track_i<num_tracks; ++track_i, ++track) {
      status |= FILE_WriteByte(ext_tag);

      // ext CC bytes (0x80..0x9f) - robotize + chord-mask + tension GRIP CCs that
      // fall outside the original cc[128] block. SEQ_CC_Get returns -1 for
      // unmapped CCs; clamp to 0 so the slot is well-defined.
      u8 cc;
      for(cc=SEQ_FILE_B_TRK_EXT_CC_FIRST; cc<=SEQ_FILE_B_TRK_EXT_CC_LAST; ++cc) {
	s32 cc_value = SEQ_CC_Get(track, cc);
	if( cc_value < 0 )
	  cc_value = 0;
	status |= FILE_WriteByte(cc_value);
      }

      // per-bar PRNG anchor seeds
      status |= FILE_WriteBuffer((u8 *)seq_cc_trk[track].robotize_bar_anchors,
				 SEQ_FILE_B_TRK_EXT_ANCHORS_SIZE);

      // V4: generator sub-block — the organism persists with its slot
      if( ext_tag == SEQ_FILE_B_TRK_EXT_TAG_V4 )
	status |= PatternGenBlockWrite(track);
    }
  }

  // fill remaining bytes with zero if required
  while( expected_pattern_size < info->header.pattern_size ) {
    status |= FILE_WriteByte(0x00);
    ++expected_pattern_size;
  }

  // close file
  status |= FILE_WriteClose();

#if DEBUG_VERBOSE_LEVEL >= 1
  DEBUG_MSG("[SEQ_FILE_B] Pattern written with status %d\n", status);
#endif

  return (status < 0) ? SEQ_FILE_B_ERR_WRITE : 0;
}


/////////////////////////////////////////////////////////////////////////////
// PHRASES cross-session probe support — stamp a slot's header as a recognizable
// EMPTY marker (20-space name + num_tracks=0). Out-of-order phrase capture
// f_lseek-expands the file past never-written slots, leaving them as UNDEFINED
// gaps (the SEQ_FILE_B_Create zero-fill is #if 0'd for FatFs). Undefined content
// could read back with any num_tracks byte, so a content-probe can't tell a real
// phrase from a gap. Writing this marker into the gap below a capture makes
// "empty" self-describing on disk, so SEQ_FILE_B_PhraseOccupancyProbe can seed
// the occupancy mask faithfully after a session reload. Only the 24-byte header
// is written; the slot body stays undefined (never read for an empty slot).
// returns < 0 on errors (error codes are documented in seq_file.h)
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_FILE_B_PatternWriteEmpty(char *session, u8 bank, u8 pattern)
{
  seq_file_b_info_t *info = SEQ_FILE_B_InfoPtr(bank);
  if( info == NULL )
    return SEQ_FILE_B_ERR_INVALID_BANK;

  if( !info->valid )
    return SEQ_FILE_B_ERR_FORMAT;

  if( pattern >= info->header.num_patterns )
    return SEQ_FILE_B_ERR_INVALID_PATTERN;

  char filepath[MAX_PATH];
  SEQ_FILE_B_BuildPath(filepath, session, bank);

  s32 status = 0;
  if( (status=FILE_WriteOpen(filepath, 0)) < 0 ) {
    FILE_WriteClose(); // important to free memory given by malloc
    return status;
  }

  // seek to the slot (f_lseek extends the file if this slot is past EOF — the
  // gap below it belongs to slots this caller also marks, so nothing undefined
  // remains readable at a phrase base)
  u32 offset = 10 + sizeof(seq_file_b_header_t) + pattern * info->header.pattern_size;
  if( (status=FILE_WriteSeek(offset)) < 0 ) {
    FILE_WriteClose();
    return status;
  }

  // 20-space name + num_tracks=0 (mixer/sysex/reserved=0) — the probe keys on
  // num_tracks (0 = never captured; a real capture writes SEQ_CORE_NUM_TRACKS_PER_GROUP)
  int i;
  for(i=0; i<20; ++i)
    status |= FILE_WriteByte(' ');
  status |= FILE_WriteByte(0x00); // num_tracks = 0  -> EMPTY
  status |= FILE_WriteByte(0x00); // mixer_map
  status |= FILE_WriteByte(0x00); // sysex_setup
  status |= FILE_WriteByte(0x00); // reserved1

  status |= FILE_WriteClose();

  return (status < 0) ? SEQ_FILE_B_ERR_WRITE : 0;
}


/////////////////////////////////////////////////////////////////////////////
// PHRASES cross-session probe — scan the MBSEQ_PH.V4 phrase bank for the
// current session and return a 16-bit occupancy mask (bit n = phrase n has
// committed records on disk). Reads only each phrase's base-pattern header
// (name + num_tracks); a slot counts as occupied when num_tracks is in
// [1, SEQ_CORE_NUM_TRACKS_PER_GROUP]. Bounded by the file size (never reads past
// EOF), and empty slots below a capture carry the SEQ_FILE_B_PatternWriteEmpty
// marker (num_tracks=0), so gaps can't false-positive. Returns 0 (nothing
// occupied) when the session has no phrase file yet — not an error.
// returns the mask (>= 0), or < 0 on a hard file error.
/////////////////////////////////////////////////////////////////////////////
// `names` (optional, NULL to skip): for each OCCUPIED phrase, copies the 20-char
// base-record name into names[n] (NUL-terminated at [20]) at zero extra I/O.
// Un-occupied / unreached slots are left untouched (the caller pre-blanks), so
// phrase names survive a session reload.
s32 SEQ_FILE_B_PhraseOccupancyProbe(char *session, char (*names)[21])
{
  // (re-)resolve + validate the phrase bank for this session
  if( SEQ_FILE_B_Open(session, SEQ_FILE_B_PHRASE_BANK) < 0 )
    return 0; // no phrase library in this session yet -> nothing occupied

  seq_file_b_info_t *info = SEQ_FILE_B_InfoPtr(SEQ_FILE_B_PHRASE_BANK);
  if( info == NULL || !info->valid )
    return 0;

  if( FILE_ReadReOpen((file_t*)&info->file) < 0 )
    return 0;

  u32 fsize = FILE_ReadGetCurrentSize();

  u16 mask = 0;
  u8 n;
  for(n=0; n<SEQ_FILE_B_NUM_PHRASES; ++n) {
    u32 offset = 10 + sizeof(seq_file_b_header_t) + (u32)(SEQ_CORE_NUM_GROUPS * n) * info->header.pattern_size;

    // need the full 24-byte header (20 name + 4) present; if this base isn't
    // fully in the file, neither is any higher phrase -> done
    if( (offset + sizeof(seq_file_b_pattern_t)) > fsize )
      break;

    if( FILE_ReadSeek(offset) < 0 )
      break;

    u8 hdr[21]; // 20-char name + num_tracks
    if( FILE_ReadBuffer(hdr, 21) < 0 )
      break;

    u8 num_tracks = hdr[20];
    if( num_tracks >= 1 && num_tracks <= SEQ_CORE_NUM_TRACKS_PER_GROUP ) {
      mask |= (1 << n);
      if( names != NULL ) {
        memcpy(names[n], hdr, 20); // the 20-char name read with the header
        names[n][20] = 0;
      }
    }
  }

  FILE_ReadClose((file_t*)&info->file);

  return (s32)mask;
}


/////////////////////////////////////////////////////////////////////////////
// PHRASES — overwrite ONLY the 20-char name field of phrase n's base (group-0)
// record in this session's MBSEQ_PH.V4, leaving the captured organism bytes
// untouched. Used by capture (stamp the phrase's name into the record so it is
// authoritative on disk) and by rename-without-recapture. `name` is read for 20
// bytes (space-padded, no terminator needed). Refuses if the phrase bank/slot
// isn't valid (the phrase must already be captured — its records must exist).
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_FILE_B_PhraseWriteName(char *session, u8 n, char *name)
{
  seq_file_b_info_t *info = SEQ_FILE_B_InfoPtr(SEQ_FILE_B_PHRASE_BANK);
  if( info == NULL )
    return SEQ_FILE_B_ERR_INVALID_BANK;
  if( !info->valid )
    return SEQ_FILE_B_ERR_FORMAT;

  u8 pattern = SEQ_CORE_NUM_GROUPS * n; // base (group-0) record of phrase n
  if( pattern >= info->header.num_patterns )
    return SEQ_FILE_B_ERR_INVALID_PATTERN;

  char filepath[MAX_PATH];
  SEQ_FILE_B_BuildPath(filepath, session, SEQ_FILE_B_PHRASE_BANK);

  s32 status = 0;
  if( (status=FILE_WriteOpen(filepath, 0)) < 0 ) {
    FILE_WriteClose(); // important to free memory given by malloc
    return status;
  }

  u32 offset = 10 + sizeof(seq_file_b_header_t) + pattern * info->header.pattern_size;
  if( (status=FILE_WriteSeek(offset)) < 0 ) {
    FILE_WriteClose();
    return status;
  }

  status |= FILE_WriteBuffer((u8 *)name, 20);
  status |= FILE_WriteClose();

  return (status < 0) ? SEQ_FILE_B_ERR_WRITE : 0;
}


/////////////////////////////////////////////////////////////////////////////
// returns a pattern name from disk w/o overwriting patterns in RAM
//
// used in SAVE menu to display the pattern name which will be overwritten
// 
// function can be called frequently w/o performance loss, as the name
// of bank/pattern will be cached.
// non_cached=1 forces an update regardless of bank/pattern number
//
// *name will contain 20 characters + 0 terminator regardless of status
//
// returns < 0 on errors (error codes are documented in seq_file.h)
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_FILE_B_PatternPeekName(u8 bank, u8 pattern, u8 non_cached, char *pattern_name)
{
  if( !non_cached && cached_bank == bank && cached_pattern == pattern ) {
    // name is in cache
    memcpy(pattern_name, cached_pattern_name, 21);
    return 0; // no error
  }

  cached_bank = bank;
  cached_pattern = pattern;

#if DEBUG_VERBOSE_LEVEL >= 2
  DEBUG_MSG("[SEQ_FILE_B] Loading Pattern Name for %d:%d\n", bank, pattern);
#endif

  // initial pattern name
  memcpy(cached_pattern_name, "-----<Disk Error>   ", 21);

  if( bank >= SEQ_FILE_B_NUM_BANKS )
    return SEQ_FILE_B_ERR_INVALID_BANK;

  seq_file_b_info_t *info = &seq_file_b_info[bank];

  if( !info->valid )
    return SEQ_FILE_B_ERR_NO_FILE;

  if( pattern >= info->header.num_patterns )
    return SEQ_FILE_B_ERR_INVALID_PATTERN;

  // re-open file
  if( FILE_ReadReOpen((file_t*)&info->file) < 0 )
    return -1; // file cannot be re-opened

  // change to file position
  s32 status;
  u32 offset = 10 + sizeof(seq_file_b_header_t) + pattern * info->header.pattern_size;
  if( (status=FILE_ReadSeek(offset)) < 0 ) {
#if DEBUG_VERBOSE_LEVEL >= 1
    DEBUG_MSG("[SEQ_FILE_B] failed to change pattern offset in file, status: %d\n", status);
#endif
    // close file (so that it can be re-opened)
    FILE_ReadClose((file_t*)&info->file);
    return SEQ_FILE_B_ERR_READ;
  }

  // read name
  status |= FILE_ReadBuffer((u8 *)cached_pattern_name, 20);
  cached_pattern_name[20] = 0;

  // close file (so that it can be re-opened)
  FILE_ReadClose((file_t*)&info->file);

  // fill category with "-----" if it is empty
  int i;
  u8 found_char = 0;
  for(i=0; i<5; ++i)
    if( cached_pattern_name[i] != ' ' ) {
      found_char = 1;
      break;
    }
  if( !found_char )
    memcpy(&cached_pattern_name[0], "-----", 5);


  // fill label with "<empty>" if it is empty
  found_char = 0;
  for(i=5; i<20; ++i)
    if( cached_pattern_name[i] != ' ' ) {
      found_char = 1;
      break;
    }
  if( !found_char )
    memcpy(&cached_pattern_name[5], "<empty>        ", 15);

  
  // copy into return variable
  memcpy(pattern_name, cached_pattern_name, 21);

#if DEBUG_VERBOSE_LEVEL >= 2
  DEBUG_MSG("[SEQ_FILE_B] Loading Pattern Name for %d:%d successfull\n", bank, pattern);
#endif

  return 0; // no error
}
