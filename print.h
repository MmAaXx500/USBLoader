#pragma once
#include <stdint.h>

struct pci_dev;

void print_string(const char *string);

void print_pci_dev(const struct pci_dev* pci_dev);

void init_output(void);

char *itoa(int value, char *str, int base);
