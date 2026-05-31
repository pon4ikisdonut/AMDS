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

    if (!cfg->quiet) {
        printf("AMDS v%s - Monitoring %d GPU(s)\n", amds_version_string(), gpu_count);
        printf("Press Ctrl+C to stop.\n\n");
    }

    while (1) {
        for (int i = 0; i < gpu_count; i++) {
            amds_poll_metrics(&gpus[i]);
            amds_poll_ras(&gpus[i]);
            print_gpu_line(&gpus[i]);
            log_stage(lg, &gpus[i], "MONITOR", "TICK");
        }
        fflush(stdout);

        if (cfg->duration_sec > 0 && (time(NULL) - start) >= cfg->duration_sec) break;
        sleep_ms((unsigned)cfg->poll_interval_ms);
    }

    return 0;
}

static int run_vram_mode(const amds_config_t *cfg, amds_gpu_t *gpus, int gpu_count, amds_logger_t *lg) {
    (void)cfg;

    if (!cfg->quiet) printf("[CLI] Starting VRAM diagnostics...\n");

    amds_ocl_ctx_t ctx;
    if (amds_ocl_init(&ctx) < 0) {
        fprintf(stderr, "AMDS: OpenCL init failed\n");
        return 1;
    }

    for (int i = 0; i < gpu_count; i++) {
        if (!cfg->quiet) printf("[CLI] Testing GPU%d (%s)...\n", gpus[i].index, gpus[i].drm_name);
        
        amds_poll_metrics(&gpus[i]);
        amds_poll_ras(&gpus[i]);

        if (!cfg->quiet) printf("  [1/5] Pattern fill/verify...\n");
        log_stage(lg, &gpus[i], "VRAM_PATTERN_BEGIN", "");
        amds_vram_test_pattern(&gpus[i], &ctx, lg);

        if (!cfg->quiet) printf("  [2/5] Walking 1s...\n");
        log_stage(lg, &gpus[i], "VRAM_WALKING_BEGIN", "");
        amds_vram_test_walking(&gpus[i], &ctx, lg);

        if (!cfg->quiet) printf("  [3/5] Moving inversions...\n");
        log_stage(lg, &gpus[i], "VRAM_MOVING_INV_BEGIN", "");
        amds_vram_test_moving_inversions(&gpus[i], &ctx, lg);

        if (!cfg->quiet) printf("  [4/5] Random noise...\n");
        log_stage(lg, &gpus[i], "VRAM_RANDOM_NOISE_BEGIN", "");
        amds_vram_test_random_noise(&gpus[i], &ctx, lg);

        if (!cfg->quiet) printf("  [5/5] PRNG test...\n");
        log_stage(lg, &gpus[i], "VRAM_PRNG_BEGIN", "");
        amds_vram_test_prng(&gpus[i], &ctx, lg);

        if (!cfg->quiet) printf("[CLI] GPU%d VRAM tests completed.\n", gpus[i].index);
    }

    amds_ocl_close(&ctx);
    if (!cfg->quiet) printf("[CLI] VRAM diagnostics finished.\n");
    return 0;
}

static int run_core_mode(const amds_config_t *cfg, amds_gpu_t *gpus, int gpu_count, amds_logger_t *lg) {
    if (!cfg->quiet) printf("[CLI] Starting GPU core stress tests...\n");

    amds_ocl_ctx_t ctx;
    if (amds_ocl_init(&ctx) < 0) {
        fprintf(stderr, "AMDS: OpenCL init failed\n");
        return 1;
    }

    amds_kmsg_monitor_start(lg);

    for (int i = 0; i < gpu_count; i++) {
        if (!cfg->quiet) printf("[CLI] Stressing GPU%d (%s)...\n", gpus[i].index, gpus[i].drm_name);
        amds_poll_metrics(&gpus[i]);
        amds_poll_ras(&gpus[i]);

        if (!cfg->quiet) printf("  [1/2] FP32 stress kernel...\n");
        amds_core_stress_fp32(&gpus[i], &ctx, lg);

        if (ctx.has_fp64) {
            if (!cfg->quiet) printf("  [2/2] FP64 stress kernel (after 20s cooling)...\n");
            log_stage(lg, &gpus[i], "COOLING_PAUSE", "20s");
            if (g_amds_logger) amds_log_printf(g_amds_logger, "[CLI] Cooling down for 20s before FP64...");
            sleep(20);

            amds_core_stress_fp64(&gpus[i], &ctx, lg);
        } else {
            if (!cfg->quiet) printf("  [2/2] FP64 stress kernel: SKIPPED (not supported)\n");
            log_stage(lg, &gpus[i], "CORE_FP64_SKIP", "cl_khr_fp64 unavailable");
        }
        if (!cfg->quiet) printf("[CLI] GPU%d core stress completed.\n", gpus[i].index);
    }

    amds_kmsg_monitor_stop();
    amds_ocl_close(&ctx);
    if (!cfg->quiet) printf("[CLI] Core stress tests finished.\n");
    return 0;
}

static int run_full_mode(const amds_config_t *cfg, amds_gpu_t *gpus, int gpu_count, amds_logger_t *lg) {
    if (!cfg->quiet) printf("[CLI] Starting FULL diagnostic suite (5 passes)...\n");

    amds_ocl_ctx_t ctx;
    if (amds_ocl_init(&ctx) < 0) {
        fprintf(stderr, "AMDS: OpenCL init failed\n");
        return 1;
    }

    amds_kmsg_monitor_start(lg);

    for (int pass = 1; pass <= 5; pass++) {
        if (!cfg->quiet) printf("[CLI] --- PASS %d/5 ---\n", pass);
        if (g_amds_logger) amds_log_printf(g_amds_logger, "[CLI] --- PASS %d/5 ---", pass);

        for (int i = 0; i < gpu_count; i++) {
            if (!cfg->quiet) printf("[CLI] GPU%d: Running VRAM tests...\n", gpus[i].index);
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

            if (!cfg->quiet) printf("[CLI] GPU%d: Running CORE tests...\n", gpus[i].index);
            amds_core_stress_fp32(&gpus[i], &ctx, lg);

            if (ctx.has_fp64) {
                log_stage(lg, &gpus[i], "COOLING_PAUSE", "20s");
                if (g_amds_logger) amds_log_printf(g_amds_logger, "[CLI] Cooling down for 20s before FP64...");
                sleep(20);

                amds_core_stress_fp64(&gpus[i], &ctx, lg);
            }

            amds_poll_metrics(&gpus[i]);
            amds_poll_ras(&gpus[i]);
            print_gpu_line(&gpus[i]);
        }
    }

    amds_kmsg_monitor_stop();
    amds_ocl_close(&ctx);
    if (!cfg->quiet) printf("[CLI] Full diagnostic suite finished.\n");
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