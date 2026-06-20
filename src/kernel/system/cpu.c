// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: cpu.c, CPU logic execptions and etc.

// This repository is licensed under the GNU General Public License.

static bool cpuid_is_available(void) {
    uint32_t before;
    uint32_t after;

    __asm__ volatile (
        "pushfl\n\t"
        "popl %0\n\t"
        "movl %0, %1\n\t"
        "xorl $0x00200000, %1\n\t"
        "pushl %1\n\t"
        "popfl\n\t"
        "pushfl\n\t"
        "popl %1\n\t"
        "pushl %0\n\t"
        "popfl\n\t"
        : "=&r"(before), "=&r"(after)
        :
        : "cc");

    return ((before ^ after) & 0x00200000u) != 0;
}

static void cpuid_query(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;

    __asm__ volatile (
        "cpuid"
        : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
        : "a"(leaf));

    if (eax != NULL) *eax = a;
    if (ebx != NULL) *ebx = b;
    if (ecx != NULL) *ecx = c;
    if (edx != NULL) *edx = d;
}

static uint64_t rdtsc_read(void) {
    uint32_t low;
    uint32_t high;

    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

