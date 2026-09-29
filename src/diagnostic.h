#ifndef GP_DIAGNOSTIC_H
#define GP_DIAGNOSTIC_H
#include "work.h"
#include "psp2_gpuprof_diagnostic.h"

/* Internal implementation of the public asynchronous diagnostic ABI. */
enum {
    GP_DIAG_UNVERIFIED = PSP2_GPUPROF_DIAG_UNVERIFIED,
    GP_DIAG_SCHEDULER_CHANGED = PSP2_GPUPROF_DIAG_SCHEDULER_CHANGED,
    GP_DIAG_PDS_CHANGED = PSP2_GPUPROF_DIAG_PDS_CHANGED,
    GP_DIAG_TAG_CHANGED = PSP2_GPUPROF_DIAG_TAG_CHANGED,
    GP_DIAG_INACTIVE = PSP2_GPUPROF_DIAG_INACTIVE,
    GP_DIAG_STAGE_DISAGREEMENT = PSP2_GPUPROF_DIAG_STAGE_DISAGREEMENT
};
int gp_diagnostic_config_valid(const Psp2GpuProfDiagnosticConfig *);
int gp_diagnostic_read(struct gp_session *, gp_work_reader, void *,
                       unsigned group, unsigned tag_group,
                       Psp2GpuProfDiagnostic *);
#endif
