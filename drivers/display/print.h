#pragma once
#include <stdint.h>
#include <uchar.h>

struct pci_dev;

void print_string(const char *string);

void print_pci_dev(const struct pci_dev *pci_dev);

void init_output(void);

char *itoa(int value, char *str, int base);

char *itoa_once(int value, int base);

char *wstr_to_str(const char16_t *wstr, uint32_t wlen, char *str, uint32_t len);
