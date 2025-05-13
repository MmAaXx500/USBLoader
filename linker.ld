SECTIONS {
    . = 0x7c00;
    .text_s1 : AT(0) { stage1.o(.text) }

    . = 0x8000;
    stage2_start = .;
    .text_s2e : AT(512) { stage2_entry.o(.text) }
    .text_s2 : { stage2.o(.text) }
    .data_s2 : { stage2.o(.data) }
    .rodata_s2 : { stage2.o(.rodata) }
    stage2_end = .;
    stage2_size = stage2_end - stage2_start;
    .bss_s2 (NOLOAD) : {
        stage2_entry.o(.bss)
        stage2.o(.bss)
    }
}
