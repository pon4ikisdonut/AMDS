#ifndef AMDS_CONFIG_H
#define AMDS_CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

typedef struct {
    bool cli_mode;
    bool quiet;
    bool adaptive;
    bool parallel;
    bool use_tui;

    char gpu_selector[256];
    char mode[64];
    char json_path[PATH_MAX];
    char csv_dir[PATH_MAX];
    char report_path[PATH_MAX];
    char log_path[PATH_MAX];

    int duration_sec;
    int poll_interval_ms;

    double max_edge_temp;
    double max_hotspot_temp;
    double max_power_w;
    double max_vddc_jitter_mv;
    double max_vddci_jitter_mv;
    double vram_fraction;
    bool skip_fp32;
    bool skip_fp64;
} amds_config_t;

void amds_config_defaults(amds_config_t *cfg);
int amds_config_parse_cli(amds_config_t *cfg, int argc, char **argv);

#endif