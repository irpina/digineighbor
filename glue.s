| SPDX-License-Identifier: GPL-2.0-or-later
| digineighbor: NEIGHBOR's descriptor and page, and its patched site
| (neighbor.c has the render's work). ColdFire V4 (MCF54418), Digitakt mk1
| OS 1.53 and 1.54. The addresses in the comments are 1.53's; the code
| takes them from os153.inc or os154.inc.
        .ifdef  OS154                   | the Digitakt mk1 1.54 (mod.json's port)
        .include "os154.inc"
        .else                           | the Digitakt mk1 1.53
        .include "os153.inc"
        .endif
        .include "digitakt-mk1/core3.inc"

        .section .run, "ax"

| At 0x40078142, the last instruction before the mixer call 0x40071c20
| (was: addi.l #0x4ba8f080,d0), when every per-track stage has run. The
| condition codes it set are not read (move.l d0,-(sp) follows).
        .globl  nb_tap_s
nb_tap_s:
        lea     -60(%sp), %sp
        movem.l %d0-%d7/%a0-%a6, (%sp)
        jsr     nb_tap
        movem.l (%sp), %d0-%d7/%a0-%a6
        lea     60(%sp), %sp
        addi.l  #TAP_ADD, %d0
        rts

| nb_copy32(dst, src): 32 longs, with separate source and destination
| registers (see neighbor.c).
        .globl  nb_copy32
nb_copy32:
        movea.l 4(%sp), %a1             | dst
        movea.l 8(%sp), %a0             | src
        moveq   #8, %d0
1:      move.l  (%a0)+, (%a1)+
        move.l  (%a0)+, (%a1)+
        move.l  (%a0)+, (%a1)+
        move.l  (%a0)+, (%a1)+
        subq.l  #1, %d0
        bne.s   1b
        rts

| NEIGHBOR, machine 4, for core's machine slots (docs/ADAPTING.md in
| elekloader, "SRC machines" and "Machine pages"): its names, its menu
| icon, SLICE's parameters, and machine 4 in the render: an empty voice
| window, which nb_inject fills (ev_render_voices). Its page, drawn by
| machine-pages, is SLICE's with PLAY, SAMP and GRID blank, SLICE as SLOT
| (the source track) and LEN as GAIN, which shows as BR's knob.
        .balign 4
        .globl  nb_machine
nb_machine:
        CM_MACHINE 4, str_long, str_short, nb_icon_bmp, 3, 4, nb_page

        .equ    P_BR, 0x86
        .balign 4
nb_page:
        CM_UI   3                                       | SLICE's page
        CM_KNOB                                         | A: TUNE
        CM_KNOB flags=CM_HIDDEN                         | B: PLAY
        CM_KNOB                                         | C: BR
        CM_KNOB flags=CM_HIDDEN                         | D: SAMP
        CM_KNOB name=str_slot, lname=str_slot_long      | E: SLICE, the source
        CM_KNOB name=str_gain, lname=str_gain_long, look=P_BR, fmt=nb_gain_text, gfx=nb_gain_gfx  | F: LEN
        CM_KNOB flags=CM_HIDDEN                         | G: GRID
        CM_KNOB                                         | H: LEV

| A Bitmap as the firmware's (the stock icons: 11 x 7, one 32-bit word a
| column, rows in bits 31-25, pixels then mask). An arrow, after the stock
| icons' opening bar:
|   #......#...
|   #.......#..
|   #........#.
|   ###########
|   #........#.
|   #.......#..
|   #......#...
        .balign 4
nb_icon_bmp:
        .long   BMP_VT, 11, 7, 1, nb_icon_px, nb_icon_mask, 0
nb_icon_px:
        .long   0xfe000000, 0x10000000, 0x10000000, 0x10000000, 0x10000000
        .long   0x10000000, 0x10000000, 0x92000000, 0x54000000, 0x38000000
        .long   0x10000000
nb_icon_mask:
        .long   0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000
        .long   0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000
        .long   0xfe000000

str_slot:       .asciz  "SLOT"
str_slot_long:  .asciz  "Source Slot"
str_gain:       .asciz  "GAIN"
str_gain_long:  .asciz  "Gain"
str_long:       .asciz  "NEIGHBOR"
str_short:      .asciz  "NBR"
        .balign 2
