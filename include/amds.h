#ifndef AMDS_H
#define AMDS_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
#include <limits.h>
#include <stdarg.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include "amds_version.h"
#include "amds_config.h"
#include "amds_gpu.h"
#include "amds_ras.h"
#include "amds_export.h"
#include "amds_analysis.h"
#include "amds_cli.h"
#include "amds_tui.h"

typedef struct {
    FILE *fp;
    int fd;
    char path[4096];
    pthread_mutex_t lock;
} amds_logger_t;

typedef struct {
    uint64_t addr;
    uint32_t expected;
    uint32_t actual;
} amds_ocl_err_t;

typedef struct {
    void *platform;
    void *device;
    void *context;
    void *queue;
    void *program_vram;
    void *program_stress;
    void *k_fill_pattern;
    void *k_verify_pattern;
    void *k_fill_lcg;
    void *k_verify_lcg;
    void *k_invert_pattern;
    void *k_fp32;
    void *k_fp64;
    void *buf_vram;
    void *buf_err_count;
    void *buf_errs;
    void *buf_stress;
    size_t vram_bytes;
    size_t vram_words;
    bool has_fp64;
    bool ready;
} amds_ocl_ctx_t;

int amds_logger_init(amds_logger_t *lg, const char *path);
void amds_logger_close(amds_logger_t *lg);
int amds_log_text(amds_logger_t *lg, const char *text);
int amds_log_printf(amds_logger_t *lg, const char *fmt, ...);

extern amds_logger_t *g_amds_logger;

int amds_mkdir_p(const char *path);
int amds_read_first_line(const char *path, char *buf, size_t sz);
int amds_read_u64(const char *path, uint64_t *v);
int amds_read_double_scaled(const char *path, double scale, double *v);
int amds_path_join(char *out, size_t outsz, const char *a, const char *b);

int amds_ocl_init(amds_ocl_ctx_t *ctx);
void amds_ocl_close(amds_ocl_ctx_t *ctx);

int amds_vram_test_pattern(amds_gpu_t *gpu, amds_ocl_ctx_t *ctx, amds_logger_t *lg);
int amds_vram_test_walking(amds_gpu_t *gpu, amds_ocl_ctx_t *ctx, amds_logger_t *lg);
int amds_vram_test_moving_inversions(amds_gpu_t *gpu, amds_ocl_ctx_t *ctx, amds_logger_t *lg);
int amds_vram_test_random_noise(amds_gpu_t *gpu, amds_ocl_ctx_t *ctx, amds_logger_t *lg);
int amds_vram_test_prng(amds_gpu_t *gpu, amds_ocl_ctx_t *ctx, amds_logger_t *lg);
void amds_vram_clear(amds_ocl_ctx_t *ctx);

int amds_kmsg_monitor_start(amds_logger_t *lg);
void amds_kmsg_monitor_stop(void);

int amds_core_stress_fp32(amds_gpu_t *gpu, amds_ocl_ctx_t *ctx, amds_logger_t *lg);
int amds_core_stress_fp64(amds_gpu_t *gpu, amds_ocl_ctx_t *ctx, amds_logger_t *lg);

#endif