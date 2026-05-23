// SEQ V4 test-control SysEx interface.
//
// Header: F0 00 00 7E 4F 54 <cmd> [args...] F7
// Reserved for harness-driven automated testing (see tests/ in repo root).

#ifndef _SEQ_TESTCTRL_H
#define _SEQ_TESTCTRL_H

#include <mios32.h>

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
