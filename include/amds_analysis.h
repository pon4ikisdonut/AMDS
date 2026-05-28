#ifndef AMDS_ANALYSIS_H
#define AMDS_ANALYSIS_H

#include "amds_gpu.h"

typedef struct {
    uint64_t total_errors;
    uint64_t single_bit_like;
    uint64_t clustered_like;
    uint64_t thermal_correlated;
    uint64_t voltage_correlated;
    char recommendation[512];
} amds_analysis_t;

void amds_analyze_gpu(const amds_gpu_t *gpu, amds_analysis_t *out);

#endif