#include "../include/amds.h"
#include <string.h>
#include <stdio.h>

static int read_err_count_file(const char *path, uint64_t *ce, uint64_t *ue) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[256];
    uint64_t local_ce = 0, local_ue = 0;

    while (fgets(line, sizeof(line), f)) {
        unsigned long long v = 0;
        if (sscanf(line, "ue: %llu", &v) == 1 || sscanf(line, "UE: %llu", &v) == 1) local_ue += v;
        if (sscanf(line, "ce: %llu", &v) == 1 || sscanf(line, "CE: %llu", &v) == 1) local_ce += v;
    }

    fclose(f);
    *ce += local_ce;
    *ue += local_ue;
    return 0;
}

static int count_bad_pages(const char *path, uint64_t *count) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[256];
    uint64_t n = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "0x")) n++;
    }
    fclose(f);
    *count = n;
    return 0;
}

int amds_ras_read(amds_gpu_t *gpu) {
    char rasdir[PATH_MAX];
    if (amds_path_join(rasdir, sizeof(rasdir), gpu->device_path, "ras") < 0) {
        if (g_amds_logger) amds_log_printf(g_amds_logger, "[RAS] no ras directory found for GPU%d", gpu->index);
        return -1;
    }

    if (g_amds_logger) amds_log_printf(g_amds_logger, "[RAS] polling RAS for GPU%d", gpu->index);
    pthread_mutex_lock(&gpu->lock);

    gpu->ras.corrected = 0;
    gpu->ras.uncorrected = 0;
    gpu->ras.bad_pages = 0;
    gpu->ras.available = false;

    static const char *files[] = {
        "gfx_err_count",
        "sdma_err_count",
        "umc_err_count",
        "mmhub_err_count",
        "athub_err_count",
        "pcie_bif_err_count",
        "hdp_err_count",
        "xgmi_wafl_err_count"
    };

    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        char p[PATH_MAX];
        if (amds_path_join(p, sizeof(p), rasdir, files[i]) == 0) {
            if (read_err_count_file(p, &gpu->ras.corrected, &gpu->ras.uncorrected) == 0) {
                gpu->ras.available = true;
                if (g_amds_logger) amds_log_printf(g_amds_logger, "[RAS] %s: CE=%lu UE=%lu", files[i], gpu->ras.corrected, gpu->ras.uncorrected);
            }
        }
    }

    char bp[PATH_MAX];
    if (amds_path_join(bp, sizeof(bp), rasdir, "gpu_vram_bad_pages") == 0) {
        if (count_bad_pages(bp, &gpu->ras.bad_pages) == 0) {
            gpu->ras.available = true;
            if (g_amds_logger) amds_log_printf(g_amds_logger, "[RAS] bad_pages: %lu", gpu->ras.bad_pages);
        }
    }

    if (g_amds_logger && gpu->ras.available) {
        amds_log_printf(g_amds_logger, "[RAS] GPU%d summary: CE=%lu UE=%lu BAD=%lu",
                        gpu->index, gpu->ras.corrected, gpu->ras.uncorrected, gpu->ras.bad_pages);
    }

    pthread_mutex_unlock(&gpu->lock);
    return 0;
}


int amds_poll_ras(amds_gpu_t *gpu) {
    return amds_ras_read(gpu);
}