#include "session.h"
#include "continuations.h"
#include <psp2kern/io/fcntl.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/proc_event.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/threadmgr.h>
#include <taihen.h>
#include <string.h>

int module_get_offset(SceUID pid, SceUID modid, int segment, size_t offset, uintptr_t *address);

static struct gp_session session;
static SceUID guard = -1, proc_handler = -1;
#define HOOK_COUNT 5u
static SceUID hooks[HOOK_COUNT] = {-1, -1, -1, -1, -1};
static tai_hook_ref_t refs[HOOK_COUNT];
static int ready, resident, abandoned;
static int device_retired;
static uint32_t dumps_in_progress;
static int startup_error = PSP2_GPUPROF_UNSUPPORTED;
static uint32_t fingerprint;
static uintptr_t code_base, data_base, registers;
static SceProcEventHandler handler;
static int (*power_try)(int owner, int system_event);
static void (*power_unlock)(int owner);
static uintptr_t (*device_node)(void);
static uintptr_t (*power_lookup)(uintptr_t list, uintptr_t predicate, uint32_t index);
static uint32_t (*init_state)(int which);

/* Startup only, outside all profiler/power locks. Never log from GPU hooks.
 * Cumulative snapshots use the file settings validated on retail hardware.
 * Fixed-size stack buffer, no formatting library, allocation or retries on error.
 */
static char report[4096];
static unsigned report_used;
#ifdef GPUPROF_BEGIN_DIAGNOSTIC
/* One completed Begin only, outside profiler/power locks. Separate file and
 * stack buffer: never race startup's report or log in a driver callback.
 */
static unsigned begin_report_claimed;
static void begin_report(int rc, uint32_t offset, uint32_t value)
{
    char line[] = "begin-internal 0x00000000 offset 0x00000000 value 0x00000000\n";
    static const unsigned positions[] = {17, 35, 52};
    static const char hex[] = "0123456789abcdef";
    uint32_t values[] = {(uint32_t)rc, offset, value};
    unsigned sent = 0;
    SceUID fd;
    if (__atomic_exchange_n(&begin_report_claimed, 1, __ATOMIC_RELAXED)) return;
    for (unsigned f = 0; f < 3; ++f)
        for (unsigned d = 0; d < 8; ++d)
            line[positions[f]+d] = hex[(values[f] >> (28-4*d)) & 15];
    fd = ksceIoOpen("ur0:tai/psp2_gpuprof_begin.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) return;
    for (unsigned tries = 0; sent < sizeof(line)-1 && tries < 8; ++tries) {
        int n = ksceIoWrite(fd, line+sent, sizeof(line)-1-sent);
        if (n <= 0 || (unsigned)n > sizeof(line)-1-sent) break;
        sent += n;
    }
    ksceIoClose(fd);
}
#endif
static void startup_log(const char *stage, int result, uint32_t detail)
{
    static const char hex[] = "0123456789abcdef";
    char line[128];
    unsigned n = 0, field, digit, sent = 0;
    uint32_t values[2] = {(uint32_t)result, detail};
    SceUID fd;
    while (*stage && n < 100) line[n++] = *stage++;
    for (field = 0; field < 2; ++field) {
        line[n++] = ' ';
        line[n++] = '0';
        line[n++] = 'x';
        for (digit = 0; digit < 8; ++digit)
            line[n++] = hex[(values[field] >> (28 - digit * 4)) & 15];
    }
    line[n++] = '\n';
    if (n > sizeof(report) - report_used) return;
    for (unsigned i = 0; i < n; ++i) report[report_used++] = line[i];
    fd = ksceIoOpen("ur0:tai/psp2_gpuprof.log",
                    SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) return;
    for (unsigned tries = 0; sent < report_used && tries < 8; ++tries) {
        int written = ksceIoWrite(fd, report + sent, report_used - sent);
        if (written <= 0 || (unsigned)written > report_used - sent) break;
        sent += (unsigned)written;
    }
    ksceIoClose(fd);
}

static int startup_offset(SceUID modid, int segment, size_t offset, uintptr_t *out)
{
    int rc = module_get_offset(KERNEL_PID, modid, segment, offset, out);
    startup_log(segment ? "data-offset" : "code-offset", rc, offset);
    return rc;
}

static uint32_t word(uintptr_t address, unsigned offset)
{
    return *(volatile uint32_t *)(address + offset);
}

static int lock(void)
{
    return ksceKernelLockMutex(guard, 1, NULL);
}

static void unlock(void)
{
    ksceKernelUnlockMutex(guard, 1);
}

static int acquire(void *ctx)
{
    uintptr_t sys, node, power, info;
    (void)ctx;
    if (!init_state(1) || !init_state(2)) return PSP2_GPUPROF_OFFLINE;
    if (power_try(-3, 0)) return PSP2_GPUPROF_BUSY;
    sys = word(data_base, 0x41e0);
    node = device_node();
    if (!sys || !node) goto offline;
    power = power_lookup(word(sys, 0x20), (code_base + 0x2d94) | 1, word(node, 8));
    if (!power || word(power, 0x1c) != 0) goto offline;
    info = word(node, 0x54);
    if (!info || !word(info, 0x20)) goto offline;
    registers = word(info, 0x14);
    if (!registers) goto offline;
    return 0;
offline:
    power_unlock(-3);
    return PSP2_GPUPROF_OFFLINE;
}

static void release(void *ctx)
{
    (void)ctx;
    power_unlock(-3);
}

static uint32_t read_reg(void *ctx, uint32_t offset)
{
    uint32_t value;
    (void)ctx;
    __asm__ volatile("dmb sy" ::: "memory");
    value = word(registers, offset);
    __asm__ volatile("dmb sy" ::: "memory");
    return value;
}

static void write_reg(void *ctx, uint32_t offset, uint32_t value)
{
    (void)ctx;
    __asm__ volatile("dmb sy" ::: "memory");
    *(volatile uint32_t *)(registers + offset) = value;
    __asm__ volatile("dsb sy" ::: "memory");
}

/* Called with guard held. Never waits for a GPU lock: it might be held by
 * the caller of one of our hooks. Keep pending restoration on contention.
 */
static int reap(void)
{
    int rc;
    if (!abandoned || !session.active) {
        abandoned = 0;
        return 0;
    }
    rc = acquire(NULL);
    if (rc) return rc;
    gp_invalidate(&session, 1);
    abandoned = 0;
    release(NULL);
    return 0;
}

static int on_exit(SceUID pid, SceProcEventInvokeParam1 *param, int arg)
{
    (void)param;
    (void)arg;
    if (lock() < 0) return 0;
    if (session.active && session.owner == pid) {
        session.owner = -1;
        abandoned = 1;
        reap();
    }
    unlock();
    return 0;
}

static int misc_hook(uintptr_t info, uint32_t *misc, uintptr_t process,
                     uintptr_t argument)
{
    int rc;
    if (!misc || misc[0] != 3)
        return gp_original_misc(info, misc, process, argument);
    if (lock() < 0) return 0x18;
    reap();
    rc = (session.active || dumps_in_progress) ? 0x18 :
        gp_original_misc(info, misc, process, argument);
    unlock();
    return rc;
}

static int pre_power_hook(uintptr_t node, int new_state, int old_state)
{
    if (lock() < 0) return 0x18;
    if (new_state != old_state && new_state != 0 && session.active) {
        /* A live session began in ON. Restore before original power-off work. */
        gp_invalidate(&session, old_state == 0);
        abandoned = 0;
    }
    unlock();
    return gp_original_power(node, new_state, old_state);
}

static int reset_hook(uintptr_t info, int recovery)
{
    if (lock() < 0) return 0x18;
    if (session.active) {
        registers = word(info, 0x14);
        gp_invalidate(&session, registers != 0);
        abandoned = 0;
    }
    unlock();
    return gp_original_reset(info, recovery);
}

static int dump_hook(void *buffer, uint32_t size, uint32_t flags)
{
    int rc;
    if (lock() < 0) return (int)0x804c0018;
    /* Recovery changes selectors outside the ordinary bridge/power lock. */
    if (session.active) gp_invalidate(&session, 1);
    abandoned = 0;
    ++dumps_in_progress;
    unlock();
    /* The original calls other OS services. Do not hold our guard across
     * those calls: a power callback may already hold one of their locks.
     * Block new captures until ALL overlapping/nested dumps have returned.
     */
    rc = gp_original_dump(buffer, size, flags);
    if (lock() >= 0) {
        --dumps_in_progress;
        unlock();
    }
    return rc;
}

static int teardown_hook(uint32_t index, uintptr_t arg1, uintptr_t arg2)
{
    int rc;
    if (lock() < 0) return 0x18;
    /* Device deinitialization eventually frees the node and system data.
     * Fence new API accesses BEFORE the original begins, not after it frees.
     * A later driver reinitialization requires a fresh profiler load/reboot.
     */
    device_retired = 1;
    ready = 0;
    startup_error = PSP2_GPUPROF_OFFLINE;
    if (session.active) {
        session.owner = -1;
        abandoned = 1;
        reap();
    }
    unlock();
    rc = gp_original_teardown(index, arg1, arg2);
    if (lock() >= 0) {
        /* Successful original teardown powers off through pre_power_hook.
         * Regardless of its result, never dereference potentially freed data.
         */
        gp_invalidate(&session, 0);
        abandoned = 0;
        registers = 0;
        unlock();
    }
    return rc;
}

int psp2GpuProfBegin(const Psp2GpuProfConfig *user_config, Psp2GpuProfSample *user_sample)
{
    Psp2GpuProfConfig cfg;
    Psp2GpuProfSample sample;
    uint32_t state;
    int rc;
#ifdef GPUPROF_BEGIN_DIAGNOSTIC
    uint32_t failure_offset = 0, failure_value = 0;
#endif
    ENTER_SYSCALL(state);
    rc = ksceKernelCopyFromUser(&cfg, user_config, sizeof(cfg));
    if (rc < 0) goto done;
    rc = lock();
    if (rc < 0) goto done;
    if (!ready) rc = startup_error;
    else if (dumps_in_progress) rc = PSP2_GPUPROF_BUSY;
    else if ((rc = reap()) == 0) {
        rc = gp_begin(&session, ksceKernelGetProcessId(), &cfg, &sample);
#ifdef GPUPROF_BEGIN_DIAGNOSTIC
        failure_offset = session.begin_failure_offset;
        failure_value = session.begin_failure_value;
#endif
    }
    if (!rc) {
        rc = ksceKernelCopyToUser(user_sample, &sample, sizeof(sample));
        if (rc < 0) {
            /* Do not strand a lease when delivery of its handle fails. */
            session.owner = -1;
            abandoned = 1;
            reap();
        }
    }
    unlock();
done:
#ifdef GPUPROF_BEGIN_DIAGNOSTIC
    begin_report(rc, failure_offset, failure_value);
#endif
    EXIT_SYSCALL(state);
    return rc;
}

int psp2GpuProfRead(const Psp2GpuProfSession *user_session, Psp2GpuProfSample *user_sample)
{
    Psp2GpuProfSession handle;
    Psp2GpuProfSample sample;
    uint32_t state;
    int rc;
    ENTER_SYSCALL(state);
    rc = ksceKernelCopyFromUser(&handle, user_session, sizeof(handle));
    if (rc < 0) goto done;
    rc = lock();
    if (rc < 0) goto done;
    rc = ready ? gp_read(&session, ksceKernelGetProcessId(), handle, &sample) : startup_error;
    if (!rc) rc = ksceKernelCopyToUser(user_sample, &sample, sizeof(sample));
    unlock();
done:
    EXIT_SYSCALL(state);
    return rc;
}

int psp2GpuProfEnd(const Psp2GpuProfSession *user_session)
{
    Psp2GpuProfSession handle;
    uint32_t state;
    int rc;
    ENTER_SYSCALL(state);
    rc = ksceKernelCopyFromUser(&handle, user_session, sizeof(handle));
    if (rc < 0) goto done;
    rc = lock();
    if (rc < 0) goto done;
    rc = ready ? gp_end(&session, ksceKernelGetProcessId(), handle) : startup_error;
    unlock();
done:
    EXIT_SYSCALL(state);
    return rc;
}

int psp2GpuProfGetInfo(Psp2GpuProfInfo *user_info)
{
    Psp2GpuProfInfo info;
    uint32_t state;
    int rc;
    ENTER_SYSCALL(state);
    rc = lock();
    if (rc < 0) goto done;
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    info.abi = PSP2_GPUPROF_ABI;
    info.driver_fingerprint = fingerprint;
    info.status = ready ? 0 : startup_error;
    info.cores = 4;
    info.counters = 8;
    info.capabilities = PSP2_GPUPROF_CAP_COUNTERS | PSP2_GPUPROF_CAP_SIGNALS;
    info.max_groups = PSP2_GPUPROF_MAX_GROUPS;
    rc = ksceKernelCopyToUser(user_info, &info, sizeof(info));
    unlock();
done:
    EXIT_SYSCALL(state);
    return rc;
}

int psp2GpuProfReadSignals(const Psp2GpuProfSignalConfig *user_config,
                          Psp2GpuProfSignals *user_signals)
{
    Psp2GpuProfSignalConfig cfg;
    Psp2GpuProfSignals signals;
    uint32_t state;
    int rc;
    ENTER_SYSCALL(state);
    rc = ksceKernelCopyFromUser(&cfg, user_config, sizeof(cfg));
    if (rc < 0) goto done;
    rc = lock();
    if (rc < 0) goto done;
    if (!ready) rc = startup_error;
    else if (dumps_in_progress) rc = PSP2_GPUPROF_BUSY;
    else if (!(rc = reap())) rc = gp_signals(&session, &cfg, &signals);
    if (!rc) rc = ksceKernelCopyToUser(user_signals, &signals, sizeof(signals));
    unlock();
done:
    EXIT_SYSCALL(state);
    return rc;
}

struct signature { uint32_t offset; unsigned char bytes[8]; };
static const struct signature signatures[] = {
    {0x4018, {0x01,0x28,0x04,0xd0,0x02,0x28,0x04,0xd0}},
    {0x4040, {0x08,0xb5,0x01,0x46,0x03,0x4b,0x18,0x68}},
    {0x4058, {0x17,0x4b,0x2d,0xe9,0xf0,0x41,0x05,0x46}},
    {0x9044, {0x01,0x4b,0x18,0x68,0x70,0x47,0x00,0xbf}},
    {0x2cd4, {0x0e,0xb4,0x03,0x46,0x77,0xb5,0x00,0x20}},
    {0x2d94, {0x43,0x69,0x0a,0x68,0x93,0x42,0x18,0xbf}},
    {0x5e68, {0x2d,0xe9,0xf3,0x47,0x0c,0x46,0x89,0x4e}},
    {0x63f8, {0x37,0x4b,0x2d,0xe9,0xf0,0x47,0x0e,0x46}},
    {0x5758, {0x2d,0xe9,0xf3,0x41,0x04,0x46,0xd0,0xf8}},
    {0x9b58, {0x2d,0xe9,0xf0,0x41,0x04,0x46,0xab,0x4f}},
    {0x4674, {0x1d,0x4b,0xf7,0xb5,0x05,0x46,0x1d,0x49}},
};

int _start(SceSize args, void *argp) __attribute__((weak, alias("module_start")));
int module_start(SceSize args, void *argp)
{
    tai_module_info_t mod;
    const struct gp_io io = {NULL, acquire, release, read_reg, write_reg};
    const uint32_t offsets[] = {0x5e68, 0x63f8, 0x5758, 0x9b58, 0x4674};
    const void *functions[] = {misc_hook, pre_power_hook, reset_hook, dump_hook, teardown_hook};
    uintptr_t end;
    unsigned i;
    int rc;
    (void)args; (void)argp;
#ifdef GPUPROF_BEGIN_DIAGNOSTIC
    startup_log("perf-enable-v1", 0, PSP2_GPUPROF_ABI);
#else
    startup_log("fixed-continuations-v1", 0, PSP2_GPUPROF_ABI);
#endif
    memset(&mod, 0, sizeof(mod));
    mod.size = sizeof(mod);
    rc = taiGetModuleInfoForKernel(KERNEL_PID, "SceGpuEs4", &mod);
    startup_log("driver-lookup", rc, 0);
    if (rc < 0)
        return SCE_KERNEL_START_FAILED;
    fingerprint = mod.module_nid;
    startup_log("driver-fingerprint",
                fingerprint == 0xc0f361a3 || fingerprint == 0x237202cb ? 0 : PSP2_GPUPROF_UNSUPPORTED,
                fingerprint);
    if (fingerprint != 0xc0f361a3 && fingerprint != 0x237202cb)
        return SCE_KERNEL_START_FAILED;
    if (startup_offset(mod.modid, 0, 0, &code_base) < 0 ||
        startup_offset(mod.modid, 0, 0x104db, &end) < 0 ||
        startup_offset(mod.modid, 1, 0, &data_base) < 0 ||
        startup_offset(mod.modid, 1, 0x41e3, &end) < 0)
        return SCE_KERNEL_START_FAILED;
    for (i = 0; i < sizeof(signatures)/sizeof(signatures[0]); ++i)
        if (memcmp((void *)(code_base + signatures[i].offset), signatures[i].bytes, 8)) {
            startup_log("signature-mismatch", PSP2_GPUPROF_UNSUPPORTED, signatures[i].offset);
            return SCE_KERNEL_START_FAILED;
        }
    startup_log("signatures-ok", 0, i);
    /* Validate the final halfword of the reset prologue's displaced LDR.W. */
    if (memcmp((void *)(code_base + 0x5760), "\xec\x37", 2)) {
        startup_log("signature-mismatch", PSP2_GPUPROF_UNSUPPORTED, 0x5760);
        return SCE_KERNEL_START_FAILED;
    }
    gp_continuations_init(code_base);
    startup_log("continuations-ready", 0, HOOK_COUNT);
    power_try = (void *)((code_base + 0x4058) | 1);
    power_unlock = (void *)((code_base + 0x4040) | 1);
    device_node = (void *)((code_base + 0x9044) | 1);
    power_lookup = (void *)((code_base + 0x2cd4) | 1);
    init_state = (void *)((code_base + 0x4018) | 1);
    gp_init(&session, &io);
    guard = ksceKernelCreateMutex("GpuProfGuard", 0, 0, NULL);
    startup_log("create-mutex", guard, 0);
    if (guard < 0) return SCE_KERNEL_START_FAILED;
    for (i = 0; i < HOOK_COUNT; ++i) {
        /* Reject intervening patches rather than bypass another hook's chain. */
        if (memcmp((void *)(code_base + offsets[i]), signatures[6+i].bytes, 8)) {
            startup_error = PSP2_GPUPROF_UNSUPPORTED;
            goto failed;
        }
        startup_log("before-install-hook", 0, offsets[i]);
        hooks[i] = taiHookFunctionOffsetForKernel(KERNEL_PID, &refs[i], mod.modid,
                                                 0, offsets[i], 1, functions[i]);
        startup_log("install-hook", hooks[i], offsets[i]);
        if (hooks[i] < 0) { startup_error = hooks[i]; goto failed; }
        resident = 1;
    }
    memset(&handler, 0, sizeof(handler));
    handler.size = sizeof(handler);
    handler.exit = on_exit;
    handler.kill = on_exit;
    proc_handler = ksceKernelRegisterProcEventHandler("GpuProfOwner", &handler, 0);
    startup_log("register-owner-handler", proc_handler, 0);
    if (proc_handler < 0) { startup_error = proc_handler; goto failed; }
    rc = lock();
    if (rc < 0) { startup_error = rc; goto failed; }
    if (!device_retired) {
        startup_error = 0;
        ready = 1;
    }
    unlock();
    startup_log("startup-result", startup_error, 1); /* Resident. */
    return SCE_KERNEL_START_SUCCESS;
failed:
    startup_log("startup-failed", startup_error, resident);
    /* Hook release does not prove every in-flight callback has returned.
     * Once code has been published, stay resident (disabled) until reboot.
     * Keep the guard alive for callbacks already dispatched into this module.
     */
    if (proc_handler >= 0) {
        rc = ksceKernelUnregisterProcEventHandler(proc_handler);
        startup_log("unregister-owner-handler", rc, 0);
    }
    for (i = HOOK_COUNT; i-- > 0;)
        if (hooks[i] >= 0) {
            rc = taiHookReleaseForKernel(hooks[i], refs[i]);
            startup_log("release-hook", rc, offsets[i]);
            if (rc >= 0) hooks[i] = -1;
        }
    startup_log("startup-result", startup_error, resident);
    if (resident) return SCE_KERNEL_START_SUCCESS;
    ksceKernelDeleteMutex(guard);
    guard = -1;
    return SCE_KERNEL_START_FAILED;
}

int module_stop(SceSize args, void *argp)
{
    (void)args; (void)argp;
    /* Explicit reboot-only lifetime; no unsafe live unloading of callbacks. */
    return SCE_KERNEL_STOP_CANCEL;
}
