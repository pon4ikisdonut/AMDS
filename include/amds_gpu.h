#ifndef AMDS_GPU_H
#define AMDS_GPU_H

#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <pthread.h>

#define AMDS_MAX_CARDS 16
#define AMDS_MAX_LABEL 128
#define AMDS_MAX_BANKS 64

typedef enum {
    AMDS_DRV_UNKNOWN = 0,
    AMDS_DRV_RADEON,
    AMDS_DRV_AMDGPU
} amds_driver_t;

typedef enum {
    AMDS_FAMILY_UNKNOWN = 0,
    AMDS_FAMILY_LEGACY,
    AMDS_FAMILY_POLARIS,
    AMDS_FAMILY_VEGA,
    AMDS_FAMILY_NAVI1X,
    AMDS_FAMILY_RDNA2,
    AMDS_FAMILY_RDNA3
} amds_family_t;

typedef enum {
    AMDS_DECODE_GENERIC = 0,
    AMDS_DECODE_HEURISTIC,
    AMDS_DECODE_FAMILY,
    AMDS_DECODE_EXACT
} amds_decode_conf_t;

typedef struct {
    char label[AMDS_MAX_LABEL];
    uint64_t errors;
} amds_bank_t;

typedef struct {
    bool valid;
    uint64_t addr;
    uint64_t rel_addr;
    uint32_t expected;
    uint32_t actual;
    int channel;
    int chip;
    int bank;
    int row;
    int column;
    amds_decode_conf_t conf;
    char label[AMDS_MAX_LABEL];
} amds_error_loc_t;

typedef struct {
    uint64_t corrected;
    uint64_t uncorrected;
    uint64_t bad_pages;
    bool available;
} amds_ras_snapshot_t;

typedef struct {
    double sclk_mhz;
    double mclk_mhz;
    double vddc_mv;
    double vddci_mv;
    double temp_edge_c;
    double temp_hotspot_c;
    double power_w;
    double power_cap_w;
    double gpu_busy_pct;
    double mem_busy_pct;
    uint64_t vram_used;
    uint64_t vram_total;
} amds_metrics_t;

typedef struct {
    int index;
    bool present;
    bool selected;

    char drm_name[64];
    char card_path[PATH_MAX];
    char device_path[PATH_MAX];
    char hwmon_path[PATH_MAX];
    char pci_slot[64];
    char pci_device_id[32];
    char subsystem_id[32];
    char vbios_version[128];
    char board_name[128];
    char memory_vendor[64];
    char memory_type[64];

    amds_driver_t driver;
    amds_family_t family;

    int memory_bus_bits;
    int memory_chips;
    int logical_banks;

    amds_metrics_t metrics;
    amds_ras_snapshot_t ras;

    pthread_mutex_t lock;
} amds_gpu_t;

const char *amds_driver_name(amds_driver_t d);
const char *amds_family_name(amds_family_t f);

int amds_discover_gpus(amds_gpu_t *gpus, int *count);
int amds_poll_metrics(amds_gpu_t *gpu);
int amds_poll_ras(amds_gpu_t *gpu);
void amds_guess_family(amds_gpu_t *gpu);
void amds_guess_memory_layout(amds_gpu_t *gpu);
void amds_decode_address(amds_gpu_t *gpu, uint64_t addr, amds_error_loc_t *out);

#endif