; out byte
; %1: IO port
; %2: data (byte)
; Clobber: AL, DX (if %1 > 255)
%macro outb 2
%ifnidni %2, al
    mov al, %2
%endif
%if %1 > 255
    mov dx, %1
    out dx, al
%else
    out %1, al
%endif
%endmacro

; in byte
; %1: IO port
; Return: AL
; Clobber: DX (if %1 > 255)
%macro inb 1
%if %1 > 255
    mov dx, %1
    in al, dx
%else
    in al, %1
%endif
%endmacro
