/* Port I/O and control registers. */
#ifndef K_IO_H
#define K_IO_H
#include "types.h"

static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }
static inline u8 inb(u16 p) { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline u32 read_cr0(void) { u32 v; __asm__ volatile("mov %%cr0, %0" : "=r"(v)); return v; }
static inline void write_cr0(u32 v) { __asm__ volatile("mov %0, %%cr0" :: "r"(v) : "memory"); }
static inline u32 read_cr2(void) { u32 v; __asm__ volatile("mov %%cr2, %0" : "=r"(v)); return v; }
static inline u32 read_cr3(void) { u32 v; __asm__ volatile("mov %%cr3, %0" : "=r"(v)); return v; }
static inline void write_cr3(u32 v) { __asm__ volatile("mov %0, %%cr3" :: "r"(v) : "memory"); }
static inline void invlpg(u32 a) { __asm__ volatile("invlpg (%0)" :: "r"(a) : "memory"); }
static inline void cli(void) { __asm__ volatile("cli" ::: "memory"); }
static inline void sti(void) { __asm__ volatile("sti" ::: "memory"); }
static inline void hlt(void) { __asm__ volatile("hlt" ::: "memory"); }
static inline u64 rdtsc(void) { u32 lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi)); return ((u64)hi << 32) | lo; }

#endif
