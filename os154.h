/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Digitakt mk1 MAIN OS 1.54: the stock data neighbor.c reads (os153.h and
 * os154.h, one per OS; neighbor.c includes the one OS154 picks). */
#define OS_TBUF       0x80001a18   /* the render's per-track blocks, 128 bytes a track */
#define OS_MACH       0x800018bc   /* the render's machine byte per track */
#define OS_VP         0x80002794   /* the render's voice parameters, 106 bytes a track */
#define OS_NOTE       0x80001f28   /* the trig's note per track, 16.16 */
#define OS_VEL        0x80001f18   /* its velocity, 8.8 */
#define OS_PITCH_TAB  0x4019b4c0   /* the firmware's pitch table */
