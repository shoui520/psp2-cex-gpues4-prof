#include "continuations.h"

/* Verified SceGpuEs4 prologues only. Some taiHEN/substitute versions fail to
 * scale Thumb literal-load offsets when generating original continuations.
 * Replay complete displaced instructions, with literal addresses resolved
 * before any hook is installed. Never execute the generated trampolines.
 * These sites require exclusive ownership: kernel.c rejects patched entries.
 */
static uintptr_t targets[10] __attribute__((used));

void gp_continuations_init(uintptr_t base)
{
    targets[0] = base + 0x6094;
    targets[1] = (base + 0x5e70) | 1;
    targets[2] = base + 0x64d8;
    targets[3] = (base + 0x6400) | 1;
    targets[4] = (base + 0x5762) | 1;
    targets[5] = base + 0x9e0c;
    targets[6] = (base + 0x9b60) | 1;
    targets[7] = base + 0x46ec;
    targets[8] = base + 0x46f0;
    targets[9] = (base + 0x467c) | 1;
    __asm__ volatile("dmb sy" ::: "memory");
}

#define LOAD_TARGET(reg, offset) \
    "movw " reg ", #:lower16:targets\n" \
    "movt " reg ", #:upper16:targets\n" \
    "ldr " reg ",[" reg ",#" offset "]\n"
#define LOAD_LITERAL(reg, offset) LOAD_TARGET(reg, offset) "ldr " reg ",[" reg "]\n"
/* Branch without clobbering any register or flags. Temporary scratch is below
 * the original live stack frame and is discarded before reaching the driver.
 */
#define RESUME(offset) \
    "push {r0,r1}\n" LOAD_TARGET("r0", offset) \
    "str r0,[sp,#4]\n" "pop {r0,pc}\n"
#define UNUSED __attribute__((unused))
#define REPLAY __attribute__((naked, noinline))

REPLAY int gp_original_misc(uintptr_t info UNUSED, uint32_t *misc UNUSED,
                            uintptr_t process UNUSED, uintptr_t argument UNUSED)
{
    __asm__ volatile("push.w {r0,r1,r4,r5,r6,r7,r8,r9,r10,lr}\n"
                     "mov r4,r1\n" LOAD_LITERAL("r6", "0") RESUME("4"));
}

REPLAY int gp_original_power(uintptr_t node UNUSED, int new_state UNUSED, int old_state UNUSED)
{
    __asm__ volatile(LOAD_LITERAL("r3", "8")
                     "push.w {r4,r5,r6,r7,r8,r9,r10,lr}\n"
                     "mov r6,r1\n" RESUME("12"));
}

REPLAY int gp_original_reset(uintptr_t info UNUSED, int recovery UNUSED)
{
    /* The eight-byte entry patch splits a four-byte instruction. Replay all
     * ten bytes and resume AFTER that instruction, not in its second half.
     */
    __asm__ volatile("push.w {r0,r1,r4,r5,r6,r7,r8,lr}\n"
                     "mov r4,r0\n" "ldr.w r3,[r0,#0x7ec]\n" RESUME("16"));
}

REPLAY int gp_original_dump(void *buffer UNUSED, uint32_t size UNUSED, uint32_t flags UNUSED)
{
    __asm__ volatile("push.w {r4,r5,r6,r7,r8,lr}\n"
                     "mov r4,r0\n" LOAD_LITERAL("r7", "20") RESUME("24"));
}

REPLAY int gp_original_teardown(uint32_t index UNUSED, uintptr_t arg1 UNUSED, uintptr_t arg2 UNUSED)
{
    __asm__ volatile(LOAD_LITERAL("r3", "28")
                     "push {r0,r1,r2,r4,r5,r6,r7,lr}\n"
                     "mov r5,r0\n" LOAD_LITERAL("r1", "32") RESUME("36"));
}
