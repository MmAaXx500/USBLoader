#pragma once
#include <stdint.h>

void print_string(const char *string);

void init_output(void);

char *itoa(int value, char *str, int base);
