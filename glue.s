| SPDX-License-Identifier: GPL-2.0-or-later
| digineighbor: the patched sites (neighbor.c has the render's work).
| ColdFire V4 (MCF54418), Digitakt mk1 OS 1.53.

        .section .run, "ax"

| At 0x40077fba in the render handler, after playback (0x400757fe) has
| written every track's block and before the overdrive stage (was: lea
| 0x4199e444,a4). By jsr: every register kept, then the replaced
| instruction.
        .globl  nb_inject_s
nb_inject_s:
        lea     -60(%sp), %sp
        movem.l %d0-%d7/%a0-%a6, (%sp)
        jsr     nb_inject
        movem.l (%sp), %d0-%d7/%a0-%a6
        lea     60(%sp), %sp
        lea     0x4199e444, %a4
        rts

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
        addi.l  #0x4ba8f080, %d0
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

| At 0x40078f72 in 0x40078f44(index, machine), a sound parameter's descriptor
| for a machine (was: moveq #3,d2 ; cmp.l d0,d2 ; bcs.s none): machines 0-3
| have 8 machine parameters each (indices 0x11-0x18), anything else none. So
| switching to 4 reset and announced nothing, and the render never took the
| new machine. NEIGHBOR (4) takes SLICE's (3), as its SRC page already does.
| By jsr, d0 = the machine: back to 0x40078f78 with d0 0-3, or to the
| "none" exit 0x40078f54 past 4. d2 is scratch there.
        .globl  nb_mapm
nb_mapm:
        moveq   #4, %d2
        cmp.l   %d0, %d2
        bcs.s   2f                      | past 4: none
        bne.s   1f                      | 0-3: as they are
        moveq   #3, %d0                 | NEIGHBOR: SLICE's parameters
1:      rts
2:      move.l  #0x40078f54, (%sp)
        rts

| The machine menu's icon. At 0x40029e9c, the entry of the menu item's icon
| callback (was: lea -12(sp),sp ; movem.l d2-d4,(sp)), by jmp: (sp) return,
| 8 the item, 12 the screen, 16 x, 20 y. The four stock icons are Bitmaps
| picked by 0x401c2e0c[machine]; past 3 it draws none. NEIGHBOR (4) gets
| nb_icon_bmp, drawn as they are: blit(screen, bitmap, x + 2, y - 1, 0),
| a tail call. Anything else goes on to the stock code.
        .equ    ITEM_ID,  0x400c3fb8    | a menu item's id (the machine)
        .equ    BLIT,     0x400c2960    | blit(dst, src, x, y, centre)
        .equ    BMP_VT,   0x401b73b4    | the firmware's Bitmap vtable
        .globl  nb_icon
nb_icon:
        move.l  8(%sp), -(%sp)
        jsr     ITEM_ID
        addq.l  #4, %sp
        moveq   #4, %d1
        cmp.l   %d1, %d0
        bne.s   1f
        move.l  12(%sp), %d0            | the screen
        move.l  %d0, 4(%sp)
        move.l  #nb_icon_bmp, %d0
        move.l  %d0, 8(%sp)
        move.l  16(%sp), %d0            | x + 2
        addq.l  #2, %d0
        move.l  %d0, 12(%sp)
        move.l  20(%sp), %d0            | y - 1
        subq.l  #1, %d0
        move.l  %d0, 16(%sp)
        clr.l   20(%sp)
        jmp     BLIT
1:      lea     -12(%sp), %sp           | the replaced instructions
        movem.l %d2-%d4, (%sp)
        jmp     0x40029ea4

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

| The SRC page. 0x400657cc(machine) returns the page's layout: 44 bytes,
| the knobs' parameter ids at +8 (A-H), 0 an empty knob (REPITCH's A is
| one). Past 3 it returns SLICE's. At its entry (was: moveq #3,d1 ; move.l
| 4(sp),d0), by jmp:
| - the machine is kept in nb_page_m. The page asks for its layout, with
|   its own track's machine, just before it draws or turns a knob
|   (0x4003aae4), so the label hooks below know whose knob it is;
| - NEIGHBOR gets its own layout: a copy of SLICE's made on first use (its
|   +0/+4 are the firmware's objects), keeping TUNE, BR, SLICE (as SLOT),
|   LEN (as GAIN, 0.5) and LEV, and emptying PLAY, SAMP and GRID;
| - anything else goes on to the stock code, at its compare.
        .equ    LAY_SLICE, 0x4197cf5c
        .globl  nb_layout
nb_layout:
        move.l  4(%sp), %d0
        move.l  %d0, nb_page_m
        moveq   #4, %d1
        cmp.l   %d1, %d0
        beq.s   1f
        moveq   #3, %d1
        jmp     0x400657d2
1:      tst.l   nb_lay_ok
        bne.s   3f
        lea     LAY_SLICE, %a0
        lea     nb_lay, %a1
        moveq   #11, %d0
2:      move.l  (%a0)+, (%a1)+
        subq.l  #1, %d0
        bne.s   2b
        lea     nb_lay, %a1
        clr.l   12(%a1)                 | B: PLAY
        clr.l   20(%a1)                 | D: SAMP
        clr.l   32(%a1)                 | G: GRID
        moveq   #1, %d0
        move.l  %d0, nb_lay_ok
3:      move.l  #nb_lay, %d0
        rts

| The labels of parameters 0x88 (SLICE's "SLICE", the source here) and
| 0x89 (its "LEN", the gain here) on a NEIGHBOR page: 0x4000fe8a(obj, id)
| gives a knob's label and 0x4000feac(obj, id) its long name (the value
| pop-up), from the parameter table alone. At their entries (was: move.l
| 8(sp),d1 ; cmpi.l #164,d1), by jmp. Their first argument is a parameter
| group, not the sound, so the machine is the one whose layout the page
| asked for last (nb_layout).
        .equ    P_SOURCE, 0x88
        .equ    P_GAIN, 0x89
        .globl  nb_lab_short, nb_lab_long
nb_lab_short:
        lea     nb_short_tab, %a0
        bsr.s   nb_lab_pick
        bne.s   9f
        move.l  8(%sp), %d1
        cmpi.l  #164, %d1
        jmp     0x4000fe94
9:      rts

nb_lab_long:
        lea     nb_long_tab, %a0
        bsr.s   nb_lab_pick
        bne.s   9f
        move.l  8(%sp), %d1
        cmpi.l  #164, %d1
        jmp     0x4000feb6
9:      rts

| a0 = {source's, gain's} names. -> d0 = the caller's id's name on a
| NEIGHBOR page, else 0; Z set when 0.
nb_lab_pick:
        moveq   #4, %d1
        cmp.l   nb_page_m, %d1
        bne.s   8f
        move.l  12(%sp), %d0            | the id (past our return address)
        cmpi.l  #P_SOURCE, %d0
        beq.s   1f
        cmpi.l  #P_GAIN, %d0
        bne.s   8f
        move.l  4(%a0), %d0
        rts
1:      move.l  (%a0), %d0
        rts
8:      moveq   #0, %d0
        rts

| -> Z clear when the id in d0 is GAIN's on a NEIGHBOR page.
nb_is_gain:
        cmpi.l  #P_GAIN, %d0
        bne.s   8f
        moveq   #4, %d1
        cmp.l   nb_page_m, %d1
        bne.s   8f
        moveq   #1, %d1
        rts
8:      moveq   #0, %d1
        rts

| GAIN's value, as text: "+6.5" under its knob, "+6.5dB" in the pop-up
| (nb_fmt_gain in neighbor.c).
|
| - 0x4000f324(obj, id, value, buf), a knob's value text, at its entry (was:
|   lea -20(sp),sp ; movem.l d2-d4/a2-a3,(sp)), by jmp.
        .globl  nb_val_text
nb_val_text:
        move.l  8(%sp), %d0
        bsr.w   nb_is_gain
        beq.s   1f
        clr.l   -(%sp)                  | no "dB"
        move.l  16(%sp), -(%sp)         | the value
        move.l  24(%sp), -(%sp)         | buf
        jsr     nb_fmt_gain
        lea     12(%sp), %sp
        rts
1:      lea     -20(%sp), %sp
        movem.l %d2-%d4/%a2-%a3, (%sp)
        jmp     0x4000f32c

| - 0x400657ee(id, value) -> the pop-up's value text (a static buffer), at
|   its entry (was: move.l 4(sp),d1 ; cmpi.l #164,d1), by jmp.
        .globl  nb_pop_text
nb_pop_text:
        move.l  4(%sp), %d0
        bsr.w   nb_is_gain
        beq.s   1f
        pea     1.w                     | with "dB"
        move.l  12(%sp), -(%sp)         | the value
        pea     nb_txt
        jsr     nb_fmt_gain
        lea     12(%sp), %sp
        rts
1:      move.l  4(%sp), %d1
        cmpi.l  #164, %d1
        jmp     0x400657f8

| GAIN's knob drawn as BR's (a round knob; LEN's is a bracket and a slice
| count): 0x4000f2bc(obj, id, value, ...) draws a knob's graphic, at its
| entry (was: lea -20(sp),sp ; movem.l d2-d6,(sp)), by jmp. For GAIN on a
| NEIGHBOR page, id becomes BR's and the value doubles (LEN's range is
| 0-63, BR's 0-127).
        .equ    P_BR, 0x86
        .globl  nb_knob_gfx
nb_knob_gfx:
        move.l  8(%sp), %d0
        bsr.w   nb_is_gain
        beq.s   1f
        move.l  #P_BR, %d0
        move.l  %d0, 8(%sp)
        move.l  12(%sp), %d0
        add.l   %d0, %d0
        move.l  %d0, 12(%sp)
1:      lea     -20(%sp), %sp
        movem.l %d2-%d6, (%sp)
        jmp     0x4000f2c4

| ... and with BR's UI record altogether: 0x40065794(id) returns a
| parameter's UI record (84 bytes at 0x4197d2f8 + 84 id: flags, the knob's
| scaling and filter, its text and graphic drawers). LEN's is a slice
| count's: a coarse knob (24 brisk notches moved it 4 steps) and no value
| text (its graphic has the number). The value itself is set by id, so it
| stays LEN's. At its entry (was: move.l 4(sp),d1 ; cmpi.l #164,d1), by
| jmp.
        .globl  nb_ui_rec
nb_ui_rec:
        move.l  4(%sp), %d0
        bsr.w   nb_is_gain
        beq.s   1f
        move.l  #P_BR, %d1
        bra.s   2f
1:      move.l  4(%sp), %d1
2:      cmpi.l  #164, %d1
        jmp     0x4006579e

        .balign 4
nb_short_tab:   .long   str_slot, str_gain
nb_long_tab:    .long   str_slot_long, str_gain_long
str_slot:       .asciz  "SLOT"
str_slot_long:  .asciz  "Source Slot"
str_gain:       .asciz  "GAIN"
str_gain_long:  .asciz  "Gain"
        .balign 2

| The machine names: 0x4007910c (long) and 0x4007912c (short) index a table
| of four and return a default string past it. Their default branches (was:
| move.l #default,d0 ; rts), by jmp, with d0 = the machine: NEIGHBOR for 4.
        .globl  nb_longname, nb_shortname
nb_longname:
        moveq   #4, %d1
        cmp.l   %d1, %d0
        bne.s   1f
        move.l  #str_long, %d0
        rts
1:      move.l  #0x401c4084, %d0
        rts

nb_shortname:
        moveq   #4, %d1
        cmp.l   %d1, %d0
        bne.s   1f
        move.l  #str_short, %d0
        rts
1:      move.l  #0x401c1d2f, %d0
        rts

str_long:   .asciz  "NEIGHBOR"
str_short:  .asciz  "NBR"
        .balign 2
