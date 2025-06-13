SECTIONS {
    . = 0x7c00;
    .text_s1 : AT(0) { stage1.o(.text) }

    . = 0x8000;
    stage2_start = .;
    .text_s2e : AT(512) { stage2_entry.o(.text) }
    .text_s2 : { *(.text) }
    .data_s2 : { *(.data) }
    .rodata_s2 : { *(.rodata) }
    stage2_end = .;
    stage2_size = stage2_end - stage2_start;
    .bss_s2 (NOLOAD) : {
        *(.bss)
        . = ALIGN(16);
        heap_start = .;
        . += 0x4000;
        heap_end = .;
        heap_size = heap_end - heap_start;
    }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.debug*)
    }
}
