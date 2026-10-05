/* Port I/O and control registers. */
#ifndef K_IO_H
#define K_IO_H
#include "types.h"

static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }
static inline u8 inb(u16 p) { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outw(u16 p, u16 v) { __asm__ volatile("outw %0, %1" :: "a"(v), "Nd"(p)); }
static inline u16 inw(u16 p) { u16 v; __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outl(u16 p, u32 v) { __asm__ volatile("outl %0, %1" :: "a"(v), "Nd"(p)); }
static inline u32 inl(u16 p) { u32 v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline u32 read_cr0(void) { u32 v; __asm__ volatile("mov %%cr0, %0" : "=r"(v)); return v; }
static inline void write_cr0(u32 v) { __asm__ volatile("mov %0, %%cr0" :: "r"(v) : "memory"); }
static inline u32 read_cr2(void) { u32 v; __asm__ volatile("mov %%cr2, %0" : "=r"(v)); return v; }
static inline u32 read_cr3(void) { u32 v; __asm__ volatile("mov %%cr3, %0" : "=r"(v)); return v; }
static inline u32 read_dr6(void) { u32 v; __asm__ volatile("mov %%dr6, %0" : "=r"(v)); return v; }
static inline void write_dr6(u32 v) { __asm__ volatile("mov %0, %%dr6" :: "r"(v)); }
static inline u32 read_dr7(void) { u32 v; __asm__ volatile("mov %%dr7, %0" : "=r"(v)); return v; }
static inline void write_dr7(u32 v) { __asm__ volatile("mov %0, %%dr7" :: "r"(v)); }
static inline void write_dr(int n, u32 v)
{
    switch (n) {
    case 0: __asm__ volatile("mov %0, %%dr0" :: "r"(v)); break;
    case 1: __asm__ volatile("mov %0, %%dr1" :: "r"(v)); break;
    case 2: __asm__ volatile("mov %0, %%dr2" :: "r"(v)); break;
    default: __asm__ volatile("mov %0, %%dr3" :: "r"(v)); break;
    }
}
static inline void write_cr3(u32 v) { __asm__ volatile("mov %0, %%cr3" :: "r"(v) : "memory"); }
static inline void invlpg(u32 a) { __asm__ volatile("invlpg (%0)" :: "r"(a) : "memory"); }
static inline void cli(void) { __asm__ volatile("cli" ::: "memory"); }
static inline void sti(void) { __asm__ volatile("sti" ::: "memory"); }
static inline void hlt(void) { __asm__ volatile("hlt" ::: "memory"); }
static inline u64 rdtsc(void) { u32 lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi)); return ((u64)hi << 32) | lo; }

#endif
