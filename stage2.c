#define VGA_LIGHT_GRAY 0x07

void write_string(const char *string)
{
    volatile char *video = (volatile char*)0xB8000;
    while(*string != 0)
    {
        *video++ = *string++;
        *video++ = VGA_LIGHT_GRAY;
    }
}

void stage2_main() {
    write_string("Hello from C!");
    while (1);
}
