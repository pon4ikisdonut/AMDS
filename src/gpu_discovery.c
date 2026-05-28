#include "../include/amds.h"
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

const char *amds_driver_name(amds_driver_t d) {
    switch (d) {
        case AMDS_DRV_RADEON: return "radeon";
        case AMDS_DRV_AMDGPU: return "amdgpu";
        default: return "unknown";
    }
}

const char *amds_family_name(amds_family_t f) {
    switch (f) {
        case AMDS_FAMILY_LEGACY: return "LEGACY";
        case AMDS_FAMILY_POLARIS: return "POLARIS";
        case AMDS_FAMILY_VEGA: return "VEGA";
        case AMDS_FAMILY_NAVI1X: return "NAVI1X";
        case AMDS_FAMILY_RDNA2: return "RDNA2";
        case AMDS_FAMILY_RDNA3: return "RDNA3";
        default: return "UNKNOWN";
    }
}

static void copy_str(char *dst, size_t dsz, const char *src) {
    if (!dst || !dsz) return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    snprintf(dst, dsz, "%s", src);
}

static void trim_nl(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = 0;
}

static void find_hwmon(const char *device_path, char *out, size_t outsz) {
    char base[PATH_MAX];
    out[0] = 0;
    if (amds_path_join(base, sizeof(base), device_path, "hwmon") < 0) return;

    DIR *d = opendir(base);
    if (!d) return;

    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        snprintf(out, outsz, "%s/%s", base, de->d_name);
        break;
    }
    closedir(d);
}

static void read_hex_text(const char *path, char *out, size_t outsz) {
    char tmp[128];
    if (amds_read_first_line(path, tmp, sizeof(tmp)) == 0) {
        trim_nl(tmp);
        copy_str(out, outsz, tmp);
    }
}

static bool starts_with_hex_id(const char *id, const char *needle) {
    if (!id || !needle) return false;
    if (!strncasecmp(id, "0x", 2)) id += 2;
    return !strcasecmp(id, needle);
}

void amds_guess_family(amds_gpu_t *gpu) {
    gpu->family = AMDS_FAMILY_UNKNOWN;

    if (gpu->driver == AMDS_DRV_RADEON) {
        gpu->family = AMDS_FAMILY_LEGACY;
        return;
    }

    if (starts_with_hex_id(gpu->pci_device_id, "67DF") ||
        starts_with_hex_id(gpu->pci_device_id, "67EF") ||
        starts_with_hex_id(gpu->pci_device_id, "67FF") ||
        starts_with_hex_id(gpu->pci_device_id, "699F") ||
        starts_with_hex_id(gpu->pci_device_id, "6995") ||
        starts_with_hex_id(gpu->pci_device_id, "6997")) {
        gpu->family = AMDS_FAMILY_POLARIS;
        return;
    }

    if (starts_with_hex_id(gpu->pci_device_id, "6863") ||
        starts_with_hex_id(gpu->pci_device_id, "687F")) {
        gpu->family = AMDS_FAMILY_VEGA;
        return;
    }

    if (starts_with_hex_id(gpu->pci_device_id, "731F") ||
        starts_with_hex_id(gpu->pci_device_id, "7310") ||
        starts_with_hex_id(gpu->pci_device_id, "7340") ||
        starts_with_hex_id(gpu->pci_device_id, "73BF")) {
        gpu->family = AMDS_FAMILY_NAVI1X;
        return;
    }

    if (starts_with_hex_id(gpu->pci_device_id, "73FF") ||
        starts_with_hex_id(gpu->pci_device_id, "73AF") ||
        starts_with_hex_id(gpu->pci_device_id, "73A5")) {
        gpu->family = AMDS_FAMILY_RDNA2;
        return;
    }

    if (starts_with_hex_id(gpu->pci_device_id, "744C") ||
        starts_with_hex_id(gpu->pci_device_id, "747E") ||
        starts_with_hex_id(gpu->pci_device_id, "73E3")) {
        gpu->family = AMDS_FAMILY_RDNA3;
        return;
    }

    if (gpu->driver == AMDS_DRV_AMDGPU) gpu->family = AMDS_FAMILY_NAVI1X;
}

void amds_guess_memory_layout(amds_gpu_t *gpu) {
    gpu->memory_bus_bits = 0;
    gpu->memory_chips = 0;
    gpu->logical_banks = 16;

    switch (gpu->family) {
        case AMDS_FAMILY_POLARIS:
            gpu->memory_bus_bits = 256;
            gpu->memory_chips = 8;
            gpu->logical_banks = 8;
            strcpy(gpu->memory_type, "GDDR5");
            break;
        case AMDS_FAMILY_VEGA:
            gpu->memory_bus_bits = 2048;
            gpu->memory_chips = 4;
            gpu->logical_banks = 16;
            strcpy(gpu->memory_type, "HBM2");
            break;
        case AMDS_FAMILY_NAVI1X:
        case AMDS_FAMILY_RDNA2:
        case AMDS_FAMILY_RDNA3:
            if (!gpu->memory_type[0]) strcpy(gpu->memory_type, "GDDR6");
            gpu->logical_banks = 16;
            break;
        case AMDS_FAMILY_LEGACY:
            if (!gpu->memory_type[0]) strcpy(gpu->memory_type, "GDDR5");
            gpu->logical_banks = 8;
            break;
        default:
            if (!gpu->memory_type[0]) strcpy(gpu->memory_type, "Unknown");
            gpu->logical_banks = 16;
            break;
    }
}

static void read_uevent_info(amds_gpu_t *gpu) {
    char path[PATH_MAX];
    if (amds_path_join(path, sizeof(path), gpu->device_path, "uevent") < 0) return;

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        trim_nl(line);

        if (!strncmp(line, "PCI_SLOT_NAME=", 14)) {
            copy_str(gpu->pci_slot, sizeof(gpu->pci_slot), line + 14);
        } else if (!strncmp(line, "DRIVER=", 7)) {
            if (!strcmp(line + 7, "amdgpu")) gpu->driver = AMDS_DRV_AMDGPU;
            else if (!strcmp(line + 7, "radeon")) gpu->driver = AMDS_DRV_RADEON;
        }
    }

    fclose(f);
}

static void read_product_name(amds_gpu_t *gpu) {
    char path[PATH_MAX], buf[256];
    const char *candidates[] = {"product_name", "product_model", "model", "product", NULL};

    for (int i = 0; candidates[i]; i++) {
        if (amds_path_join(path, sizeof(path), gpu->device_path, candidates[i]) == 0) {
            if (amds_read_first_line(path, buf, sizeof(buf)) == 0) {
                trim_nl(buf);
                if (buf[0]) {
                    copy_str(gpu->board_name, sizeof(gpu->board_name), buf);
                    return;
                }
            }
        }
    }

    if (!gpu->board_name[0]) {
        snprintf(gpu->board_name, sizeof(gpu->board_name), "%s (%s)", 
                 amds_family_name(gpu->family), gpu->pci_device_id);
    }
}


int amds_discover_gpus(amds_gpu_t *gpus, int *count) {
    *count = 0;
    memset(gpus, 0, sizeof(amds_gpu_t) * AMDS_MAX_CARDS);

    DIR *d = opendir("/sys/class/drm");
    if (!d) return -1;

    struct dirent *de;
    while ((de = readdir(d))) {
        if (strncmp(de->d_name, "card", 4) != 0) continue;
        if (strchr(de->d_name, '-')) continue;
        if (*count >= AMDS_MAX_CARDS) break;

        amds_gpu_t *gpu = &gpus[*count];
        memset(gpu, 0, sizeof(*gpu));
        gpu->index = *count;
        gpu->present = true;
        gpu->selected = true;
        pthread_mutex_init(&gpu->lock, NULL);

        strncpy(gpu->drm_name, de->d_name, sizeof(gpu->drm_name) - 1);
        gpu->drm_name[sizeof(gpu->drm_name) - 1] = '\0';
        snprintf(gpu->card_path, sizeof(gpu->card_path), "/sys/class/drm/%s", de->d_name);
        snprintf(gpu->device_path, sizeof(gpu->device_path), "%s/device", gpu->card_path);

        char path[PATH_MAX], linkbuf[PATH_MAX];
        snprintf(path, sizeof(path), "%s/driver", gpu->device_path);
        ssize_t n = readlink(path, linkbuf, sizeof(linkbuf) - 1);
        if (n > 0) {
            linkbuf[n] = 0;
            if (strstr(linkbuf, "amdgpu")) gpu->driver = AMDS_DRV_AMDGPU;
            else if (strstr(linkbuf, "radeon")) gpu->driver = AMDS_DRV_RADEON;
        }

        read_uevent_info(gpu);

        if (amds_path_join(path, sizeof(path), gpu->device_path, "device") == 0)
            read_hex_text(path, gpu->pci_device_id, sizeof(gpu->pci_device_id));

        if (amds_path_join(path, sizeof(path), gpu->device_path, "subsystem_device") == 0)
            read_hex_text(path, gpu->subsystem_id, sizeof(gpu->subsystem_id));

        find_hwmon(gpu->device_path, gpu->hwmon_path, sizeof(gpu->hwmon_path));
        amds_guess_family(gpu);
        amds_guess_memory_layout(gpu);
        read_product_name(gpu);

        (*count)++;
    }

    closedir(d);
    return *count;
}