/* Host-harness shadow of the SH68F90A SDCC register header.
 * The dynamic keymap module only needs types + bit/flag macros that
 * keycodes.h may reference; no SFRs are touched in the code under
 * test. Provide the minimal surface so the real board kbdef.h and
 * keycodes.h compile on the host. */
#ifndef HARNESS_SH68F90A_H
#define HARNESS_SH68F90A_H

#include <stdint.h>
#include <stdbool.h>

/* SDCC memory-class keywords -> host no-ops.
 * NOTE: __bit is already defined in harness_stubs.h. */
#define __xdata
#define __code
#define __data
#define __reentrant
#define __interrupt(x)
#define __naked
#define __at(x)
#define __sbit(x)
#define __sfr(x)
#define __using(x)
#define __critical
#define __endasm
#define __asm

#endif /* HARNESS_SH68F90A_H */
