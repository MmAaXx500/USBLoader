#pragma once
#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
	__asm__ volatile("outb %b0, %w1"
	                 :
	                 : "a"(val), "Nd"(port)
	                 : "memory");
}

static inline void outl(uint16_t port, uint32_t val) {
	__asm__ volatile("outl %k0, %w1"
	                 :
	                 : "a"(val), "Nd"(port)
	                 : "memory");
}

static inline uint8_t inb(uint16_t port) {
	uint8_t ret;
	__asm__ volatile("inb %w1, %b0"
	                 : "=a"(ret)
	                 : "Nd"(port)
	                 : "memory");
	return ret;
}

static inline uint32_t inl(uint16_t port) {
	uint32_t ret;
	__asm__ volatile("inl %w1, %k0"
	                 : "=a"(ret)
	                 : "Nd"(port)
	                 : "memory");
	return ret;
}
