#include "../include/amds.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

static void print_gpu_line(const amds_gpu_t *g) {
    printf("GPU%d %-6s %-8s %-12s SCLK=%6.0f MCLK=%6.0f EDGE=%5.1f HOT=%5.1f PWR=%6.1f GPU=%5.0f%% MEM=%5.0f%% VRAM=%6.2f/%6.2f GiB RAS[CE=%llu UE=%llu BAD=%llu]\n",
        g->index,
        g->drm_name,
        amds_driver_name(g->driver),
        amds_family_name(g->family),
        g->metrics.sclk_mhz,
        g->metrics.mclk_mhz,
        g->metrics.temp_edge_c,
        g->metrics.temp_hotspot_c,
        g->metrics.power_w,
        g->metrics.gpu_busy_pct,
        g->metrics.mem_busy_pct,
        g->metrics.vram_used / 1073741824.0,
        g->metrics.vram_total / 1073741824.0,
        (unsigned long long)g->ras.corrected,
        (unsigned long long)g->ras.uncorrected,
        (unsigned long long)g->ras.bad_pages
    );
}

static void log_stage(amds_logger_t *lg, amds_gpu_t *g, const char *stage, const char *extra) {
    char line[1024];
    snprintf(line, sizeof(line),
             "[%ld] [%s] [GPU%d %s %s EDGE=%.1f HOT=%.1f PWR=%.1f GPU=%.0f%% MEM=%.0f%%] %s",
             (long)time(NULL),
             stage,
             g->index,
             g->drm_name,
             amds_family_name(g->family),
             g->metrics.temp_edge_c,
             g->metrics.temp_hotspot_c,
             g->metrics.power_w,
             g->metrics.gpu_busy_pct,
             g->metrics.mem_busy_pct,
             extra ? extra : "");
    amds_log_text(lg, line);
}

static void sleep_ms(unsigned ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;

    while (nanosleep(&ts, &ts) == -1) {
    }
}

static int run_monitor_mode(const amds_config_t *cfg, amds_gpu_t *gpus, int gpu_count, amds_logger_t *lg) {
    time_t start = time(NULL);

    while (1) {
        printf("=== AMDS %s ===\n", amds_version_string());
        for (int i = 0; i < gpu_count; i++) {
            amds_poll_metrics(&gpus[i]);
            amds_poll_ras(&gpus[i]);
            print_gpu_line(&gpus[i]);
            log_stage(lg, &gpus[i], "MONITOR", "TICK");
        }
        printf("\n");
        fflush(stdout);

        if (cfg->duration_sec > 0 && (time(NULL) - start) >= cfg->duration_sec) break;
        sleep_ms((unsigned)cfg->poll_interval_ms);
    }

    return 0;
}

static int run_vram_mode(const amds_config_t *cfg, amds_gpu_t *gpus, int gpu_count, amds_logger_t *lg) {
    (void)cfg;

    amds_ocl_ctx_t ctx;
    if (amds_ocl_init(&ctx) < 0) {
        fprintf(stderr, "AMDS: OpenCL init failed\n");
        return 1;
    }

    for (int i = 0; i < gpu_count; i++) {
        amds_poll_metrics(&gpus[i]);
        amds_poll_ras(&gpus[i]);

        log_stage(lg, &gpus[i], "VRAM_PATTERN_BEGIN", "");
        amds_vram_test_pattern(&gpus[i], &ctx, lg);

        log_stage(lg, &gpus[i], "VRAM_WALKING_BEGIN", "");
        amds_vram_test_walking(&gpus[i], &ctx, lg);

        log_stage(lg, &gpus[i], "VRAM_MOVING_INV_BEGIN", "");
        amds_vram_test_moving_inversions(&gpus[i], &ctx, lg);

        log_stage(lg, &gpus[i], "VRAM_RANDOM_NOISE_BEGIN", "");
        amds_vram_test_random_noise(&gpus[i], &ctx, lg);

        log_stage(lg, &gpus[i], "VRAM_PRNG_BEGIN", "");
        amds_vram_test_prng(&gpus[i], &ctx, lg);
    }

    amds_ocl_close(&ctx);
    return 0;
}

static int run_core_mode(const amds_config_t *cfg, amds_gpu_t *gpus, int gpu_count, amds_logger_t *lg) {
    (void)cfg;

    amds_ocl_ctx_t ctx;
    if (amds_ocl_init(&ctx) < 0) {
        fprintf(stderr, "AMDS: OpenCL init failed\n");
        return 1;
    }

    amds_kmsg_monitor_start(lg);

    for (int i = 0; i < gpu_count; i++) {
        amds_poll_metrics(&gpus[i]);
        amds_poll_ras(&gpus[i]);

        amds_core_stress_fp32(&gpus[i], &ctx, lg);

        if (ctx.has_fp64) {
            log_stage(lg, &gpus[i], "COOLING_PAUSE", "20s");
            if (g_amds_logger) amds_log_printf(g_amds_logger, "[CLI] Cooling down for 20s before FP64...");
            sleep(20);

            amds_core_stress_fp64(&gpus[i], &ctx, lg);
        } else {
            log_stage(lg, &gpus[i], "CORE_FP64_SKIP", "cl_khr_fp64 unavailable");
        }
    }

    amds_kmsg_monitor_stop();
    amds_ocl_close(&ctx);
    return 0;
}

static int run_full_mode(const amds_config_t *cfg, amds_gpu_t *gpus, int gpu_count, amds_logger_t *lg) {
    (void)cfg;

    amds_ocl_ctx_t ctx;
    if (amds_ocl_init(&ctx) < 0) {
        fprintf(stderr, "AMDS: OpenCL init failed\n");
        return 1;
    }

    amds_kmsg_monitor_start(lg);

    for (int pass = 1; pass <= 5; pass++) {
        if (g_amds_logger) amds_log_printf(g_amds_logger, "[CLI] --- PASS %d/5 ---", pass);

        for (int i = 0; i < gpu_count; i++) {
            amds_poll_metrics(&gpus[i]);
            amds_poll_ras(&gpus[i]);

            log_stage(lg, &gpus[i], "VRAM_PATTERN_BEGIN", "");
            amds_vram_test_pattern(&gpus[i], &ctx, lg);

            log_stage(lg, &gpus[i], "VRAM_WALKING_BEGIN", "");
            amds_vram_test_walking(&gpus[i], &ctx, lg);

            log_stage(lg, &gpus[i], "VRAM_MOVING_INV_BEGIN", "");
            amds_vram_test_moving_inversions(&gpus[i], &ctx, lg);

            log_stage(lg, &gpus[i], "VRAM_RANDOM_NOISE_BEGIN", "");
            amds_vram_test_random_noise(&gpus[i], &ctx, lg);

            log_stage(lg, &gpus[i], "VRAM_PRNG_BEGIN", "");
            amds_vram_test_prng(&gpus[i], &ctx, lg);

            amds_core_stress_fp32(&gpus[i], &ctx, lg);

            if (ctx.has_fp64) {
                log_stage(lg, &gpus[i], "COOLING_PAUSE", "20s");
                if (g_amds_logger) amds_log_printf(g_amds_logger, "[CLI] Cooling down for 20s before FP64...");
                sleep(20);

                amds_core_stress_fp64(&gpus[i], &ctx, lg);
            } else {
                log_stage(lg, &gpus[i], "CORE_FP64_SKIP", "cl_khr_fp64 unavailable");
            }

            amds_poll_metrics(&gpus[i]);
            amds_poll_ras(&gpus[i]);
            print_gpu_line(&gpus[i]);
        }
    }

    amds_kmsg_monitor_stop();
    amds_ocl_close(&ctx);
    return 0;
}

int amds_run_cli(const amds_config_t *cfg, amds_gpu_t *gpus, int gpu_count) {
    amds_logger_t *lg = g_amds_logger;
    bool own_lg = false;

    if (!lg) {
        lg = calloc(1, sizeof(amds_logger_t));
        if (amds_logger_init(lg, cfg->log_path) < 0) {
            fprintf(stderr, "AMDS: failed to open log file\n");
            free(lg);
            return 1;
        }
        own_lg = true;
    }

    if (g_amds_logger) amds_log_printf(g_amds_logger, "[CLI] starting in mode %s", cfg->mode);

    int rc = 0;
    if (!strcmp(cfg->mode, "monitor")) {
        rc = run_monitor_mode(cfg, gpus, gpu_count, lg);
    } else if (!strcmp(cfg->mode, "vram")) {
        rc = run_vram_mode(cfg, gpus, gpu_count, lg);
    } else if (!strcmp(cfg->mode, "core")) {
        rc = run_core_mode(cfg, gpus, gpu_count, lg);
    } else if (!strcmp(cfg->mode, "full")) {
        rc = run_full_mode(cfg, gpus, gpu_count, lg);
    } else {
        fprintf(stderr, "AMDS: unknown mode '%s'\n", cfg->mode);
        rc = 1;
    }

    if (g_amds_logger) amds_log_printf(g_amds_logger, "[CLI] finished with code %d", rc);
    if (own_lg) { amds_logger_close(lg); free(lg); }
    return rc;
}