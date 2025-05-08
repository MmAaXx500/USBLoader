[BITS 16]
enter_protected:
    cli
    lgdt [gdtr]
    mov eax, cr0
    or al, 1
    mov cr0, eax

    jmp 0x8:protected_init

%define GDT_LIMIT(seg)  (((seg)  & 0x0000ffff) | \
                       ((((seg)  & 0x000f0000) >> 16) << 48))
%define GDT_BASE(base) ((((base) & 0x00ffffff)        << 16) | \
                       ((((base) & 0xff000000) >> 24) << 56))
%define GDT_TYPE(type)       (((type)    & 0x1f) << 40)
%define GDT_PRIV_LVL(lvl)    (((lvl)     & 0x3)  << 45)
%define GDT_PRESENT(present) (((present) & 0x1)  << 47)
%define GDT_ATTS(atts)       (((atts)    & 0x7)  << 52)
%define GDT_GRAN(gran)   (((gran)  & 0x1)  << 55)

gdtr:
    dw ((gdt_end - gdt))-1
    dd gdt

gdt:
    ;ALIGN 16
    dq 0x0   ; null entry

    ; Code segment
    ; Limit: 4GiB (gran = 1)
    ; Base: start at 0
    ; Type: system, executable, grows up, conforming, rw, not accessed
    ; Privilege level: ring 0
    ; Present:  must be 1
    ; Attributes: 32-bit protected mode
    ; Granularity: 4KiB blocks
    dq GDT_LIMIT(0x0fffff) | GDT_BASE(0x0) | GDT_TYPE(0x1a) | GDT_PRIV_LVL(0x0) | GDT_PRESENT(0x1) | GDT_ATTS(0x4) | GDT_GRAN(0x1)

    ; Data segment
    ; Limit: 4GiB (gran = 1)
    ; Base: start at 0
    ; Type: system, not executable, grows up, conforming, rw, not accessed
    ; Privilege level: ring 0
    ; Present:  must be 1
    ; Attributes: 32-bit protected mode
    ; Granularity: 4KiB blocks
    dq GDT_LIMIT(0x0fffff) | GDT_BASE(0x0) | GDT_TYPE(0x12) | GDT_PRIV_LVL(0x0) | GDT_PRESENT(0x1) | GDT_ATTS(0x4) | GDT_GRAN(0x1)
gdt_end:

[BITS 32]
extern stage2_main

protected_init:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov esp, 0x7bfe  ; stack is before the bootloader (16 bit align)

    call stage2_main


