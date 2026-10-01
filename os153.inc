| SPDX-License-Identifier: GPL-2.0-or-later
| Digitakt mk1 MAIN OS 1.53: the stock code and data glue.s uses (os153.inc and
| os154.inc, one per OS; glue.s includes the one OS154 picks).
        .equ BMP_VT,       0x401b73b4    | the firmware's Bitmap vtable
        .equ LAY_SLICE,    0x4197cf5c    | SLICE's SRC page layout
        .equ INJECT_LEA,   0x4199e444    | what the inject site's lea loads into a4
        .equ TAP_ADD,      0x4ba8f080    | what the tap site's addi adds to d0
        .equ LAYOUT_ON,    0x400657d2    | the page layout, past its site
        .equ UI_REC_ON,    0x4006579e    | the UI record, past its site
        .equ POP_TEXT_ON,  0x400657f8    | the pop-up text, past its site
        .equ LAB_SHORT_ON, 0x4000fe94    | the short label, past its site
        .equ LAB_LONG_ON,  0x4000feb6    | the long label, past its site
        .equ VAL_TEXT_ON,  0x4000f32c    | a knob's value text, past its site
        .equ KNOB_GFX_ON,  0x4000f2c4    | a knob's graphic, past its site
