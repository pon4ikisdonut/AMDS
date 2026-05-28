#include "../include/amds.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;

static void on_sig(int sig) {
    (void)sig;
    g_stop = 1;
}

static void log_start(amds_logger_t *lg, amds_gpu_t *gpus, int gpu_count, const amds_config_t *cfg) {
    char line[1024];
    snprintf(line, sizeof(line),
             "[%ld] [INIT] [AMDS=%s] [MODE=%s] [GPU_COUNT=%d] [POLL_MS=%d] [DURATION=%d]",
             (long)time(NULL), amds_version_string(), cfg->mode, gpu_count, cfg->poll_interval_ms, cfg->duration_sec);
    amds_log_text(lg, line);

    for (int i = 0; i < gpu_count; i++) {
        snprintf(line, sizeof(line),
                 "[%ld] [GPU_DISCOVERED] [GPU%d %s %s %s DEV=%s PCI=%s MEM=%s BUS=%d CHIPS=%d]",
                 (long)time(NULL),
                 gpus[i].index,
                 gpus[i].drm_name,
                 amds_driver_name(gpus[i].driver),
                 amds_family_name(gpus[i].family),
                 gpus[i].pci_device_id[0] ? gpus[i].pci_device_id : "-",
                 gpus[i].pci_slot[0] ? gpus[i].pci_slot : "-",
                 gpus[i].memory_type[0] ? gpus[i].memory_type : "-",
                 gpus[i].memory_bus_bits,
                 gpus[i].memory_chips);
        amds_log_text(lg, line);
    }
}

static void destroy_gpu_mutexes(amds_gpu_t *gpus, int gpu_count) {
    for (int i = 0; i < gpu_count; i++) {
        pthread_mutex_destroy(&gpus[i].lock);
    }
}

int main(int argc, char **argv) {
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    amds_config_t cfg;
    amds_config_defaults(&cfg);
    amds_config_parse_cli(&cfg, argc, argv);

    amds_gpu_t gpus[AMDS_MAX_CARDS];
    int gpu_count = 0;

    if (amds_discover_gpus(gpus, &gpu_count) <= 0) {
        fprintf(stderr, "AMDS: no AMD GPUs found\n");
        return 1;
    }

    int j = 0;
    for (int i = 0; i < gpu_count; i++) {
        if (gpus[i].driver == AMDS_DRV_AMDGPU || gpus[i].driver == AMDS_DRV_RADEON) {
            if (i != j) {
                gpus[j] = gpus[i];
            }
            j++;
        }
    }
    gpu_count = j;

    if (gpu_count <= 0) {
        fprintf(stderr, "AMDS: no AMD GPUs found after filtering\n");
        return 1;
    }

    amds_logger_t logger;
    memset(&logger, 0, sizeof(logger));
    if (amds_logger_init(&logger, cfg.log_path) == 0) {
        g_amds_logger = &logger;
        log_start(&logger, gpus, gpu_count, &cfg);
    }

    int rc = 0;
    if (cfg.cli_mode) {
        rc = amds_run_cli(&cfg, gpus, gpu_count);
    } else {
        rc = amds_run_tui(&cfg, gpus, gpu_count);
    }

    if (g_amds_logger) amds_log_printf(g_amds_logger, "[INIT] starting exports");
    amds_export_json(&cfg, gpus, gpu_count);
    amds_export_csv(&cfg, gpus, gpu_count);
    amds_export_report(&cfg, gpus, gpu_count);
    if (g_amds_logger) amds_log_printf(g_amds_logger, "[INIT] exports finished");

    if (g_amds_logger) amds_log_printf(g_amds_logger, "[EXIT] process finished with code %d", rc);
    amds_logger_close(&logger);
    destroy_gpu_mutexes(gpus, gpu_count);
    return rc;
}