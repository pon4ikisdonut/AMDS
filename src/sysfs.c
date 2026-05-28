#include "../include/amds.h"
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ctype.h>

static int copy_str(char *dst, size_t dsz, const char *src) {
    if (!dst || !dsz) return -1;
    if (!src) {
        dst[0] = 0;
        return 0;
    }
    size_t n = strlen(src);
    if (n >= dsz) n = dsz - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
    return 0;
}

static int path_join2(char *out, size_t outsz, const char *a, const char *b) {
    int r = snprintf(out, outsz, "%s/%s", a, b);
    return (r < 0 || (size_t)r >= outsz) ? -1 : 0;
}

static int read_first_line(const char *path, char *buf, size_t sz) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(buf, sz, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    size_t n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    return 0;
}

static int read_u64_file(const char *path, uint64_t *v) {
    char b[128];
    if (read_first_line(path, b, sizeof(b)) < 0) return -1;
    *v = strtoull(b, NULL, 0);
    return 0;
}

static int read_hex_id(const char *path, char *out, size_t outsz) {
    char b[128];
    if (read_first_line(path, b, sizeof(b)) < 0) return -1;
    copy_str(out, outsz, b);
    return 0;
}

static int read_double_scale(const char *path, double scale, double *v) {
    uint64_t t;
    if (read_u64_file(path, &t) < 0) return -1;
    *v = (double)t / scale;
    return 0;
}

static int read_current_dpm(const char *path, double *mhz) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strchr(line, '*')) {
            char *p = strchr(line, ':');
            if (!p) continue;
            double val = 0.0;
            if (sscanf(p + 1, "%lf", &val) == 1) {
                *mhz = val;
                fclose(f);
                return 0;
            }
        }
    }
    fclose(f);
    return -1;
}

static void find_hwmon(const char *device_path, char *out, size_t outsz) {
    char base[PATH_MAX];
    out[0] = 0;
    if (path_join2(base, sizeof(base), device_path, "hwmon") < 0) return;

    DIR *d = opendir(base);
    if (!d) return;

    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        if (path_join2(out, outsz, base, de->d_name) == 0) {
            closedir(d);
            return;
        }
    }
    closedir(d);
}

static bool is_polaris_id(const char *id) {
    if (!id || !*id) return false;
    const char *p = id;
    if (!strncmp(p, "0x", 2) || !strncmp(p, "0X", 2)) p += 2;

    static const char *known[] = {
        "67DF","67EF","67FF","67E0","67E1","67E3","67E8","67EB",
        "699F","6995","6997","67C4","67C7","67D0","67D1"
    };
    for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
        if (!strcasecmp(p, known[i])) return true;
    }
    return false;
}

static amds_bank_map_t detect_bank_map(amds_gpu_info_t *g) {
    if (is_polaris_id(g->pci_device_id)) return AMDS_MAP_POLARIS_8CH;
    return AMDS_MAP_GENERIC;
}

static void fill_layout_defaults(amds_gpu_info_t *g) {
    g->memory_chips = 0;
    g->memory_bus_bits = 0;
    g->channel_count = 0;
    g->bytes_per_burst = 32;
    g->mmio_base_guess = 0;

    if (g->bank_map == AMDS_MAP_POLARIS_8CH) {
        g->memory_chips = 8;
        g->memory_bus_bits = 256;
        g->channel_count = 8;
        g->bytes_per_burst = 32;
    }
}

int amds_detect_gpus(amds_app_t *app) {
    memset(app->gpus, 0, sizeof(app->gpus));
    app->gpu_count = 0;

    DIR *d = opendir("/sys/class/drm");
    if (!d) return -1;

    struct dirent *de;
    while ((de = readdir(d))) {
        if (strncmp(de->d_name, "card", 4) != 0) continue;
        if (strchr(de->d_name, '-')) continue;
        if (app->gpu_count >= AMDS_MAX_CARDS) break;

        char cardroot[PATH_MAX], devlink[PATH_MAX], driverlink[PATH_MAX], target[PATH_MAX];
        if (path_join2(cardroot, sizeof(cardroot), "/sys/class/drm", de->d_name) < 0) continue;
        if (path_join2(devlink, sizeof(devlink), cardroot, "device") < 0) continue;
        if (path_join2(driverlink, sizeof(driverlink), devlink, "driver") < 0) continue;

        amds_gpu_info_t *g = &app->gpus[app->gpu_count];
        memset(g, 0, sizeof(*g));
        g->card_index = app->gpu_count;
        copy_str(g->card_path, sizeof(g->card_path), cardroot);
        copy_str(g->device_path, sizeof(g->device_path), devlink);
        copy_str(g->drm_name, sizeof(g->drm_name), de->d_name);
        g->present = true;

        ssize_t n = readlink(driverlink, target, sizeof(target) - 1);
        if (n > 0) {
            target[n] = 0;
            if (strstr(target, "amdgpu")) g->driver = AMDS_DRV_AMDGPU;
            else if (strstr(target, "radeon")) g->driver = AMDS_DRV_RADEON;
        }

        find_hwmon(devlink, g->hwmon_path, sizeof(g->hwmon_path));

        char fpath[PATH_MAX];
        if (path_join2(fpath, sizeof(fpath), devlink, "device") == 0)
            read_hex_id(fpath, g->pci_device_id, sizeof(g->pci_device_id));
        if (path_join2(fpath, sizeof(fpath), devlink, "vendor") == 0)
            read_hex_id(fpath, g->vendor_name, sizeof(g->vendor_name));
        if (path_join2(fpath, sizeof(fpath), devlink, "resource0") == 0)
            read_u64_file(fpath, &g->mmio_base_guess);

        if (path_join2(fpath, sizeof(fpath), devlink, "uevent") == 0) {
            FILE *f = fopen(fpath, "r");
            if (f) {
                char line[256];
                while (fgets(line, sizeof(line), f)) {
                    if (!strncmp(line, "PCI_SLOT_NAME=", 14)) {
                        copy_str(g->pci_slot, sizeof(g->pci_slot), line + 14);
                        size_t ln = strlen(g->pci_slot);
                        while (ln && (g->pci_slot[ln - 1] == '\n' || g->pci_slot[ln - 1] == '\r')) g->pci_slot[--ln] = 0;
                    }
                }
                fclose(f);
            }
        }

        if (is_polaris_id(g->pci_device_id)) copy_str(g->device_name, sizeof(g->device_name), "Polaris");
        else copy_str(g->device_name, sizeof(g->device_name), "AMD GPU");

        g->bank_map = detect_bank_map(g);
        fill_layout_defaults(g);
        app->gpu_count++;
    }

    closedir(d);
    return app->gpu_count;
}

int amds_refresh_gpu_metrics(amds_app_t *app) {
    for (int i = 0; i < app->gpu_count; i++) {
        amds_gpu_info_t *g = &app->gpus[i];
        char p[PATH_MAX];
        double v;

        if (g->driver == AMDS_DRV_AMDGPU) {
            if (path_join2(p, sizeof(p), g->device_path, "gpu_busy_percent") == 0)
                if (read_double_scale(p, 1.0, &v) == 0) g->gpu_busy_pct = v;

            if (path_join2(p, sizeof(p), g->device_path, "mem_busy_percent") == 0)
                if (read_double_scale(p, 1.0, &v) == 0) g->mem_busy_pct = v;

            if (path_join2(p, sizeof(p), g->device_path, "mem_info_vram_used") == 0)
                read_u64_file(p, &g->vram_used);

            if (path_join2(p, sizeof(p), g->device_path, "mem_info_vram_total") == 0)
                read_u64_file(p, &g->vram_total);

            if (path_join2(p, sizeof(p), g->device_path, "pp_dpm_sclk") == 0)
                if (read_current_dpm(p, &v) == 0) g->sclk_mhz = v;

            if (path_join2(p, sizeof(p), g->device_path, "pp_dpm_mclk") == 0)
                if (read_current_dpm(p, &v) == 0) g->mclk_mhz = v;
        }

        if (g->hwmon_path[0]) {
            if (path_join2(p, sizeof(p), g->hwmon_path, "freq1_input") == 0)
                if (read_double_scale(p, 1000000.0, &v) == 0) g->sclk_mhz = v;

            if (path_join2(p, sizeof(p), g->hwmon_path, "freq2_input") == 0)
                if (read_double_scale(p, 1000000.0, &v) == 0) g->mclk_mhz = v;

            if (path_join2(p, sizeof(p), g->hwmon_path, "in0_input") == 0)
                if (read_double_scale(p, 1.0, &v) == 0) g->vddc_mv = v;

            if (path_join2(p, sizeof(p), g->hwmon_path, "temp1_input") == 0)
                if (read_double_scale(p, 1000.0, &v) == 0) g->temp_edge_c = v;

            if (path_join2(p, sizeof(p), g->hwmon_path, "temp2_input") == 0)
                if (read_double_scale(p, 1000.0, &v) == 0) g->temp_hotspot_c = v;

            if (path_join2(p, sizeof(p), g->hwmon_path, "power1_average") == 0)
                if (read_double_scale(p, 1000000.0, &v) == 0) g->power_w = v;

            if (path_join2(p, sizeof(p), g->hwmon_path, "power1_cap") == 0)
                if (read_double_scale(p, 1000000.0, &v) == 0) g->power_cap_w = v;
        }
    }
    return 0;
}

int amds_init_banks(amds_app_t *app) {
    amds_gpu_info_t *g = &app->gpus[0];

    if (g->bank_map == AMDS_MAP_POLARIS_8CH) {
        app->bank_count = 8;
        for (int i = 0; i < app->bank_count; i++) {
            snprintf(app->banks[i].label, sizeof(app->banks[i].label), "Chip %d / Channel %c", i, 'A' + i);
            app->banks[i].errors = 0;
        }
        return 0;
    }

    app->bank_count = 16;
    for (int i = 0; i < app->bank_count; i++) {
        snprintf(app->banks[i].label, sizeof(app->banks[i].label), "Byte %d", i);
        app->banks[i].errors = 0;
    }
    return 0;
}

static int polaris_channel_from_rel(uint64_t rel_addr) {
    int burst_slot = (int)((rel_addr >> 5) & 0x7);
    static const int burst_to_channel[8] = {0, 4, 2, 6, 1, 5, 3, 7};
    return burst_to_channel[burst_slot & 7];
}

static int polaris_chip_from_channel(int channel) {
    static const int channel_to_chip[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    return channel_to_chip[channel & 7];
}

void amds_map_addr_to_bank(amds_app_t *app, uint64_t addr, amds_vram_error_t *out) {
    memset(out, 0, sizeof(*out));
    out->valid = true;
    out->failed_addr = addr;

    amds_gpu_info_t *g = &app->gpus[0];
    uint64_t rel = addr;

    if (g->mmio_base_guess && addr >= g->mmio_base_guess)
        rel = addr - g->mmio_base_guess;

    out->rel_addr = rel;
    out->byte_lane = rel & 0x1F;
    out->word_lane = (rel >> 2) & 0x7;
    out->burst_index = rel >> 5;
    out->burst_slot = (rel >> 5) & 0x7;
    out->stripe_id = (rel >> 8) & 0x3F;

    if (g->bank_map == AMDS_MAP_POLARIS_8CH) {
        out->channel_id = polaris_channel_from_rel(rel);
        out->chip_id = polaris_chip_from_channel(out->channel_id);
        out->bank_id = out->chip_id % app->bank_count;
        snprintf(out->bank_label, sizeof(out->bank_label),
                 "Chip %d / Channel %c", out->chip_id, 'A' + out->channel_id);
        return;
    }

    out->channel_id = out->burst_slot;
    out->chip_id = out->burst_slot;
    out->bank_id = out->burst_slot % app->bank_count;
    snprintf(out->bank_label, sizeof(out->bank_label), "%s", app->banks[out->bank_id].label);
}

const char *amds_map_name(amds_bank_map_t m) {
    switch (m) {
        case AMDS_MAP_POLARIS_8CH: return "POLARIS_8CH";
        default: return "GENERIC";
    }
}