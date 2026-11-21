.section .rodata
.balign 8
.global g_font_serif_start
.global g_font_serif_end
g_font_serif_start:
    .incbin "assets/fonts/InstrumentSerif-Regular.ttf"
g_font_serif_end:

.balign 8
.global g_font_serif_it_start
.global g_font_serif_it_end
g_font_serif_it_start:
    .incbin "assets/fonts/InstrumentSerif-Italic.ttf"
g_font_serif_it_end:

.balign 8
.global g_font_sans_start
.global g_font_sans_end
g_font_sans_start:
    .incbin "assets/fonts/Outfit-SemiBold.ttf"
g_font_sans_end:

.balign 8
.global g_font_semi_start
.global g_font_semi_end
g_font_semi_start:
    .incbin "assets/fonts/Outfit-Bold.ttf"
g_font_semi_end:
