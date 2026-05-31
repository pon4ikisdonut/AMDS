#include "../include/amds.h"
#include <string.h>
#include <stdlib.h>

void amds_config_defaults(amds_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->adaptive = true;
    cfg->parallel = true;
    cfg->use_tui = true;
    strcpy(cfg->gpu_selector, "all");
    strcpy(cfg->mode, "monitor");
    strcpy(cfg->json_path, "exports/amds.json");
    strcpy(cfg->csv_dir, "exports");
    strcpy(cfg->report_path, "exports/report.md");
    strcpy(cfg->log_path, "./amds_diag.log");
    cfg->duration_sec = 60;
    cfg->poll_interval_ms = 1000;
    cfg->max_edge_temp = 85.0;
    cfg->max_hotspot_temp = 100.0;
    cfg->max_power_w = 350.0;
    cfg->max_vddc_jitter_mv = 35.0;
    cfg->max_vddci_jitter_mv = 50.0;
    cfg->vram_fraction = 0.9;
    cfg->skip_fp32 = false;
    cfg->skip_fp64 = false;
    }

static int need_arg(int i, int argc) {
    return i + 1 < argc;
}

static void print_help(void) {
    printf("AMDS v%s - AMD GPU Diagnostics Suite\n\n", AMDS_VERSION_STRING);
    printf("Usage: amds [options]\n\n");
    printf("General Options:\n");
    printf("  --cli                Enable CLI mode (disable TUI)\n");
    printf("  --gpu <id>           Select GPU index or 'all' (default: all)\n");
    printf("  --mode <mode>        Set diagnostic mode: monitor, vram, core, full (default: monitor)\n");
    printf("  --duration <sec>     Test duration in seconds (for monitor mode, default: 60)\n");
    printf("  --poll-ms <ms>       Polling interval in milliseconds (default: 1000)\n");
    printf("  --quiet              Reduce output verbosity\n");
    printf("  --help, -h           Show this help message\n");
    printf("  --version, -v        Show version information\n\n");
    printf("Thresholds & Limits:\n");
    printf("  --max-edge-temp <c>    Edge temperature threshold for adaptive throttling (default: 85.0)\n");
    printf("  --max-hotspot-temp <c> Hotspot temperature threshold (default: 100.0)\n");
    printf("  --max-power <w>        Power consumption threshold (default: 350.0)\n");
    printf("  --no-adaptive          Disable temperature-based stress test throttling\n\n");
    printf("Export Options:\n");
    printf("  --json <path>        Path for JSON telemetry export (default: exports/amds.json)\n");
    printf("  --csv-dir <dir>      Directory for CSV telemetry export (default: exports)\n");
    printf("  --report <path>      Path for Markdown report (default: exports/report.md)\n");
    printf("  --log <path>         Path for diagnostic log (default: ./amds_diag.log)\n\n");
}

int amds_config_parse_cli(amds_config_t *cfg, int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--cli")) {
            cfg->cli_mode = true;
            cfg->use_tui = false;
        } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            print_help();
            exit(0);
        } else if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-v")) {
            printf("AMDS v%s\n", AMDS_VERSION_STRING);
            exit(0);
        } else if (!strcmp(argv[i], "--quiet")) {
            cfg->quiet = true;
        } else if (!strcmp(argv[i], "--gpu") && need_arg(i, argc)) {
            strncpy(cfg->gpu_selector, argv[++i], sizeof(cfg->gpu_selector) - 1);
        } else if (!strcmp(argv[i], "--mode") && need_arg(i, argc)) {
            strncpy(cfg->mode, argv[++i], sizeof(cfg->mode) - 1);
        } else if (!strcmp(argv[i], "--duration") && need_arg(i, argc)) {
            cfg->duration_sec = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--poll-ms") && need_arg(i, argc)) {
            cfg->poll_interval_ms = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--json") && need_arg(i, argc)) {
            strncpy(cfg->json_path, argv[++i], sizeof(cfg->json_path) - 1);
        } else if (!strcmp(argv[i], "--csv-dir") && need_arg(i, argc)) {
            strncpy(cfg->csv_dir, argv[++i], sizeof(cfg->csv_dir) - 1);
        } else if (!strcmp(argv[i], "--report") && need_arg(i, argc)) {
            strncpy(cfg->report_path, argv[++i], sizeof(cfg->report_path) - 1);
        } else if (!strcmp(argv[i], "--log") && need_arg(i, argc)) {
            strncpy(cfg->log_path, argv[++i], sizeof(cfg->log_path) - 1);
        } else if (!strcmp(argv[i], "--max-edge-temp") && need_arg(i, argc)) {
            cfg->max_edge_temp = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--max-hotspot-temp") && need_arg(i, argc)) {
            cfg->max_hotspot_temp = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--max-power") && need_arg(i, argc)) {
            cfg->max_power_w = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--no-adaptive")) {
            cfg->adaptive = false;
        }
    }
    return 0;
}