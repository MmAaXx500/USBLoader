#pragma once
#include <stdint.h>

struct free_block {
    uint32_t size;
    struct free_block *next;
};

struct block_header {
    uint32_t size;
    uint32_t start_off;
};

extern struct free_block *free_block_head;
