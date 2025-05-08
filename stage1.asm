COM1 equ 0x3f8
COMTEST_TRIES equ 10000

extern stage2_start
extern stage2_size

%macro outb 2
    mov al, %2
%if %1 > 255
    mov dx, %1
    out dx, al
%else
    out %1, al
%endif
%endmacro

%macro inb 1
%if %1 > 255
    mov dx, %1
    in al, dx
%else
    in al, %1
%endif
%endmacro

; Print one charecter in AL
; Uses: AX, BX
%macro PrintCharBios 0
    mov bh, 0
    mov ah, 0x0e    ; Teletype Output
    int 0x10        ; IN: AH = 0x0E
                    ;     AL = character
                    ;     BH = page num
                    ;     BL = foreground color (video only)
%endmacro

; Print one charecter in AL
; Uses: AX, DX
%macro PrintCharCom 0
    mov ah, al
%%loop:
    inb COM1 + 5    ; read Line Status Register
    and al, 0x20    ; check if Transmitter holding register empty is set
    jz %%loop
    mov al, ah
    outb COM1, al
%endmacro

; Print one charecter in AL
%macro PrintChar 1
    push ax
    push bx
    push dx
    mov al, %1
    PrintCharCom
    PrintCharBios
    pop dx
    pop bx
    pop ax
%endmacro

[BITS 16]
start:
    jmp 0x0:start2 ; set up cs
start2:
    xor ax, ax
    mov ds, ax
    mov ss, ax      ; stack segment
    mov sp, 0x7bfe  ; stack is before the bootloader (16 bit align)
    mov [bootdisk], dl
    cld

    outb COM1 + 1, 0x00 ; disable interrupts
    outb COM1 + 3, 0x80 ; set DLAB (Divisor Latch Access Bit)
    outb COM1 + 0, 0x0c ; set divisor to 12 (9600 baud), low byte
    outb COM1 + 1, 0x00 ;                                high byte
    outb COM1 + 3, 0x03 ; clear DLAB, no parity, stop bits 1, data bits 8
    outb COM1 + 2, 0xc7 ; 14 byte trigger, clear tx & rx FIFO, enable FIFO
    outb COM1 + 4, 0x1f ; loopback, ena OUT2, ena OUT1, RTS set, DTR set
    outb COM1 + 0, 0xae ; send test byte

    ; It seems that real hardware (at least Asus CUSL2-C) requires some time
    ; to be able to return the test byte
    mov cx, COMTEST_TRIES
.comtest_loop:
    inb COM1 + 5    ; read Line Status Register
    and al, 0x1
    jnz .comtest_ok ; test if data available
    loop .comtest_loop
    jmp hang

.comtest_ok:
    outb COM1 + 4, 0x0f ; normal operation, ena OUT2, ena OUT1, RTS set, DTR set

    mov ax, rst_disk
    call LogString

    mov ah, 0x0 ; reset disks
    mov dl, [bootdisk]
    int 0x13    ; Input AH = 0, DL disk number
                ; DL set by the BIOS

    jc some_err
    mov ax, done
    call LogString

    mov ax, read_disk
    call LogString

    ; calcualte stage2 size in sectors
    mov ax, stage2_size
    add ax, 512 + 1
    shr ax, 9   ; div 512
    push ax     ; push stage2 size in sectors

    mov ch, 0           ; cylinder
    mov cl, 2           ; sector
    mov dh, 0           ; head
    mov dl, [bootdisk]  ; bootdisk set by BIOS

    mov ax, 0               ; stage2 segment
    mov es, ax
    mov bx, stage2_start    ; stage2 offsett

read_loop:

    mov ah, 0x02    ; read sectors
    mov al, 1       ; read 1 sector
    int 0x13        ; Input: AH = 0x02
                    ;        AL = num of sectors to read
                    ;        CH = cylinder number
                    ;        CL = sector number (0-5 bits)
                    ;        DH = head number
                    ;        DL = drive number
                    ;        ES:BX = data buffer
                    ; Out: CF set on error / CF clear on successful
                    ;      AH = status
                    ;      AL = number of sectors read

    jc some_err

.offset_check:
    add bx, 512 ; next memory location
    jc .offset_roll
.sector_check:
    cmp cl, 18
    je .sector_roll
    inc cl      ; next sector
.cylinder_check:
    cmp ch, 80
    jnb .cylinder_roll
.head_check:
    cmp dh, 2
    jnb .head_roll
    jmp .cont_read

; ========== ROLLOVERS ==========

.offset_roll:
    mov bx, es
    add bx, 0x1000
    mov es, bx
    xor bx, bx
    jmp .sector_check

.sector_roll:
    mov cl, 1   ; back to sector 1
    inc ch      ; next cylinder
    jmp .cylinder_check

.cylinder_roll:
    xor ch, ch  ; back to cylinder 0
    inc dh      ; next head
    jmp .head_check

.head_roll:
    xor dh, dh

; ========== ROLLOVERS END ==========

.cont_read:
    PrintChar "."

    mov si, cx  ; save ch/cl
    pop cx
    dec cx      ; dec remaining sectors
    push cx     ; restore ch/cl
    mov cx, si

    jnz read_loop

    pop cx      ; just to clear the stack

    mov ax, done
    call LogString

    jmp jump_stage2

some_err:
    mov ax, err
    call LogString
hang:
    mov ax, hang_msg
    call LogString
    jmp $

jump_stage2:
    mov ax, jump_stage
    call LogString
    jmp 0x0:stage2_start


; Log the specified string to COM1 and the screen
; In: AX: string ptr
LogString:
    push si
    push bx
    push dx
    mov si, ax

.loop:
    lodsb
    or al, al
    jz .done
    PrintCharCom
    PrintCharBios
    jmp .loop

.done:
    pop dx
    pop bx
    pop si
    ret

rst_disk   db 'Reset disks ', 0
read_disk db 'Read ', 0
done db 'OK', 13, 10, 0
err db 'Err', 13, 10, 0
hang_msg db 'hang', 13, 10, 0
jump_stage db 'JMP stage2', 13, 10, 0

bootdisk db 0

    times 510-($-$$) db 0xcc
    db 0x55
    db 0xAA
