// SEQ V4 test-control SysEx interface.
//
// Header: F0 00 00 7E 4F 54 <cmd> [args...] F7
// Reserved for harness-driven automated testing (see tests/ in repo root).
//
// SEQ_TESTCTRL_ENABLE gates the whole control surface. It is ON by default so
// the HIL harness build is unchanged. Build the gig/release firmware with
// `make TESTCTRL=0` to compile it OUT: the surface is always-listening on every
// MIDI-in port and can mutate banks/sessions/CCs/FREEZE behind a 6-byte header,
// which is a footgun in a live set. Disabled, Init/Parser/TimeOut are no-op stubs.

#ifndef _SEQ_TESTCTRL_H
#define _SEQ_TESTCTRL_H

#include <mios32.h>

#ifndef SEQ_TESTCTRL_ENABLE
#define SEQ_TESTCTRL_ENABLE 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern s32 SEQ_TESTCTRL_Init(u32 mode);
extern s32 SEQ_TESTCTRL_Parser(mios32_midi_port_t port, u8 midi_in);
extern s32 SEQ_TESTCTRL_TimeOut(mios32_midi_port_t port);

#ifdef __cplusplus
}
#endif

#endif /* _SEQ_TESTCTRL_H */
