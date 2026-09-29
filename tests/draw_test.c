#include "psp2_gpuprof_draw.h"
#include <assert.h>
int main(void)
{
    Psp2GpuProfSignalConfig cfg = psp2GpuProfFragmentDrawConfig();
    assert(cfg.size == sizeof(cfg) && cfg.abi == PSP2_GPUPROF_ABI);
    assert(cfg.group_count == 1 && cfg.groups[0] == 17);
    for (unsigned i=1; i<8; ++i) assert(!cfg.groups[i]);
    for (unsigned batch=0; batch<8192; ++batch)
        for (unsigned active=0; active<2; ++active) {
            uint32_t raw = 0xc000ffffu | batch<<16 | active<<29;
            Psp2GpuProfDrawObservation d = psp2GpuProfDecodePdsDraw(raw);
            assert(d.raw == raw && d.batch == batch && d.active == active);
            assert(d.candidate_draw == (active && batch ? (int)batch-1 : -1));
        }
    return 0;
}
