#define _GNU_SOURCE
#include "../include/amds.h"
#include <string.h>
#include <stdio.h>
#include <strings.h>

static int read_metric_u64(const char *base, const char *name, uint64_t *v) {
    char path[PATH_MAX];
    if (amds_path_join(path, sizeof(path), base, name) < 0) return -1;
    return amds_read_u64(path, v);
}

static int read_metric_double(const char *base, const char *name, double scale, double *v) {
    char path[PATH_MAX];
    if (amds_path_join(path, sizeof(path), base, name) < 0) return -1;
    return amds_read_double_scaled(path, scale, v);
}

static int read_current_dpm(const char *device_path, const char *name, double *mhz) {
    char path[PATH_MAX];
    if (amds_path_join(path, sizeof(path), device_path, name) < 0) return -1;

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (!strchr(line, '*')) continue;
        char *p = strchr(line, ':');
        if (!p) continue;
        double val = 0.0;
        if (sscanf(p + 1, "%lf", &val) == 1) {
            fclose(f);
            *mhz = val;
            return 0;
        }
    }

    fclose(f);
    return -1;
}

int amds_poll_metrics(amds_gpu_t *gpu) {
    pthread_mutex_lock(&gpu->lock);

    if (gpu->driver == AMDS_DRV_AMDGPU) {
        read_metric_double(gpu->device_path, "gpu_busy_percent", 1.0, &gpu->metrics.gpu_busy_pct);
        read_metric_double(gpu->device_path, "mem_busy_percent", 1.0, &gpu->metrics.mem_busy_pct);
        read_metric_u64(gpu->device_path, "mem_info_vram_used", &gpu->metrics.vram_used);
        read_metric_u64(gpu->device_path, "mem_info_vram_total", &gpu->metrics.vram_total);

        if (read_current_dpm(gpu->device_path, "pp_dpm_sclk", &gpu->metrics.sclk_mhz) < 0)
            read_metric_double(gpu->hwmon_path, "freq1_input", 1000000.0, &gpu->metrics.sclk_mhz);

        if (read_current_dpm(gpu->device_path, "pp_dpm_mclk", &gpu->metrics.mclk_mhz) < 0)
            read_metric_double(gpu->hwmon_path, "freq2_input", 1000000.0, &gpu->metrics.mclk_mhz);
    }

    if (gpu->hwmon_path[0]) {
        read_metric_double(gpu->hwmon_path, "freq1_input", 1000000.0, &gpu->metrics.sclk_mhz);
        read_metric_double(gpu->hwmon_path, "freq2_input", 1000000.0, &gpu->metrics.mclk_mhz);
        read_metric_double(gpu->hwmon_path, "in0_input", 1.0, &gpu->metrics.vddc_mv);
        read_metric_double(gpu->hwmon_path, "temp1_input", 1000.0, &gpu->metrics.temp_edge_c);

        bool found_hotspot = false;
        for (int i = 1; i <= 8; i++) {
            char lpath[PATH_MAX], lname[32], label[64];
            snprintf(lname, sizeof(lname), "temp%d_label", i);
            if (amds_path_join(lpath, sizeof(lpath), gpu->hwmon_path, lname) == 0) {
                if (amds_read_first_line(lpath, label, sizeof(label)) == 0) {
                    if (strcasestr(label, "junction") || strcasestr(label, "hotspot")) {
                        char ipath[32];
                        snprintf(ipath, sizeof(ipath), "temp%d_input", i);
                        read_metric_double(gpu->hwmon_path, ipath, 1000.0, &gpu->metrics.temp_hotspot_c);
                        found_hotspot = true;
                        break;
                    }
                }
            }
        }
        if (!found_hotspot) {
            read_metric_double(gpu->hwmon_path, "temp2_input", 1000.0, &gpu->metrics.temp_hotspot_c);
        }

        if (read_metric_double(gpu->hwmon_path, "power1_average", 1000000.0, &gpu->metrics.power_w) < 0) {
            if (read_metric_double(gpu->hwmon_path, "power1_input", 1000000.0, &gpu->metrics.power_w) < 0) {
                read_metric_double(gpu->hwmon_path, "power2_average", 1000000.0, &gpu->metrics.power_w);
            }
        }
        read_metric_double(gpu->hwmon_path, "power1_cap", 1000000.0, &gpu->metrics.power_cap_w);
        read_metric_double(gpu->hwmon_path, "fan1_input", 1.0, &gpu->metrics.fan_rpm);
    }

    if (g_amds_logger) {
        amds_log_printf(g_amds_logger, "[METRIC] GPU%d EDGE=%.1f HOT=%.1f PWR=%.1f FAN=%.0f SCLK=%.1f MCLK=%.1f BUSY=%.0f%% MEM=%.0f%% VRAM=%lu/%lu",
                        gpu->index, gpu->metrics.temp_edge_c, gpu->metrics.temp_hotspot_c, gpu->metrics.power_w,
                        gpu->metrics.fan_rpm,
                        gpu->metrics.sclk_mhz, gpu->metrics.mclk_mhz, gpu->metrics.gpu_busy_pct, gpu->metrics.mem_busy_pct,
                        gpu->metrics.vram_used, gpu->metrics.vram_total);
    }

    pthread_mutex_unlock(&gpu->lock);
    return 0;
}

void amds_decode_address(amds_gpu_t *gpu, uint64_t addr, amds_error_loc_t *out) {
    memset(out, 0, sizeof(*out));
    out->valid = true;
    out->addr = addr;
    out->rel_addr = addr;
    out->row = -1;
    out->column = -1;
    out->bank = -1;

    switch (gpu->family) {
        case AMDS_FAMILY_POLARIS: {
            int burst_slot = (int)((addr >> 5) & 0x7);
            static const int burst_to_channel[8] = {0, 4, 2, 6, 1, 5, 3, 7};
            out->channel = burst_to_channel[burst_slot];
            out->chip = out->channel;
            out->bank = out->chip;
            out->conf = AMDS_DECODE_FAMILY;
            snprintf(out->label, sizeof(out->label), "Chip %d / Channel %c", out->chip, 'A' + out->channel);
            break;
        }
        case AMDS_FAMILY_VEGA:
            out->channel = (int)((addr >> 8) & 0xF);
            out->chip = out->channel >> 2;
            out->bank = out->channel;
            out->conf = AMDS_DECODE_HEURISTIC;
            snprintf(out->label, sizeof(out->label), "HBM Stack %d / Ch %d", out->chip, out->channel);
            break;
        case AMDS_FAMILY_NAVI1X:
        case AMDS_FAMILY_RDNA2:
        case AMDS_FAMILY_RDNA3:
            out->channel = (int)((addr >> 8) & 0xF);
            out->chip = out->channel;
            out->bank = out->channel;
            out->conf = AMDS_DECODE_HEURISTIC;
            snprintf(out->label, sizeof(out->label), "GDDR6 Lane %d", out->channel);
            break;
        case AMDS_FAMILY_LEGACY:
            out->channel = (int)((addr >> 5) & 0x7);
            out->chip = out->channel;
            out->bank = out->channel;
            out->conf = AMDS_DECODE_HEURISTIC;
            snprintf(out->label, sizeof(out->label), "Legacy Byte/Chip %d", out->channel);
            break;
        default:
            out->channel = (int)(addr & 0xF);
            out->chip = out->channel;
            out->bank = out->channel;
            out->conf = AMDS_DECODE_GENERIC;
            snprintf(out->label, sizeof(out->label), "Generic Bank %d", out->bank);
            break;
    }
}