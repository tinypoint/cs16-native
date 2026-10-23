#ifndef CS16_GUEST_CPU_H
#define CS16_GUEST_CPU_H
#include <stdint.h>
#include <time.h>
/* An explicit virtual 1 GHz timestamp counter for lifted RDTSC. This is not
 * a claim of cycle-accurate x86 timing; game timing still needs validation. */
static inline uint64_t cs16_guest_rdtsc(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC,&ts);
    return (uint64_t)ts.tv_sec*1000000000u+(uint64_t)ts.tv_nsec;
}
#define __rdtsc cs16_guest_rdtsc
#endif
