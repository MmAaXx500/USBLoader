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
