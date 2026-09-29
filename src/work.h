#ifndef GP_WORK_H
#define GP_WORK_H
#include "session.h"
#include "psp2_gpuprof_work.h"
/* Called only under acquired device power lock. Read-only; return an error
 * rather than dereference an unavailable scheduler allocation. */
typedef int (*gp_work_reader)(void *, Psp2GpuProfWorkState *);
int gp_work(struct gp_session *, gp_work_reader, void *, Psp2GpuProfWork *);
#endif
