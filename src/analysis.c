#include "../include/amds.h"
#include <string.h>
#include <stdio.h>

void amds_analyze_gpu(const amds_gpu_t *gpu, amds_analysis_t *out) {
    memset(out, 0, sizeof(*out));

    out->total_errors = gpu->ras.corrected + gpu->ras.uncorrected + gpu->ras.bad_pages;
    out->single_bit_like = gpu->ras.corrected;
    out->clustered_like = gpu->ras.bad_pages;
    out->thermal_correlated = gpu->metrics.temp_hotspot_c > 95.0 ? 1 : 0;
    out->voltage_correlated = gpu->metrics.vddc_mv < 700.0 ? 1 : 0;

    if (gpu->ras.bad_pages > 0) {
        snprintf(out->recommendation, sizeof(out->recommendation),
                 "RAS bad pages detected. Treat this GPU as hardware-suspect. Focus on VRAM chips, rails, and thermal stability.");
    } else if (gpu->ras.uncorrected > 0) {
        snprintf(out->recommendation, sizeof(out->recommendation),
                 "Uncorrected RAS errors detected. Prioritize memory stress, power integrity checks, and board-level inspection.");
    } else if (gpu->ras.corrected > 0) {
        snprintf(out->recommendation, sizeof(out->recommendation),
                 "Corrected RAS errors detected. Hardware may still be marginal under stress or temperature.");
    } else {
        snprintf(out->recommendation, sizeof(out->recommendation),
                 "No RAS evidence of persistent faults from current snapshot. Continue active stress and logging.");
    }
}