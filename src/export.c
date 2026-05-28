#include "../include/amds.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

static int ensure_parent_dirs(const amds_config_t *cfg) {
    char tmp[4096];

    snprintf(tmp, sizeof(tmp), "%s", cfg->csv_dir);
    if (amds_mkdir_p(tmp) < 0) return -1;

    snprintf(tmp, sizeof(tmp), "%s", "exports");
    if (amds_mkdir_p(tmp) < 0) return -1;

    return 0;
}

int amds_export_json(const amds_config_t *cfg, amds_gpu_t *gpus, int gpu_count) {
    if (ensure_parent_dirs(cfg) < 0) return -1;

    FILE *f = fopen(cfg->json_path, "w");
    if (!f) return -1;

    fprintf(f, "{\n");
    fprintf(f, "  \"version\": \"%s\",\n", amds_version_string());
    fprintf(f, "  \"timestamp\": %ld,\n", (long)time(NULL));
    fprintf(f, "  \"gpu_count\": %d,\n", gpu_count);
    fprintf(f, "  \"gpus\": [\n");

    for (int i = 0; i < gpu_count; i++) {
        amds_gpu_t *g = &gpus[i];
        fprintf(f, "    {\n");
        fprintf(f, "      \"index\": %d,\n", g->index);
        fprintf(f, "      \"drm_name\": \"%s\",\n", g->drm_name);
        fprintf(f, "      \"driver\": \"%s\",\n", amds_driver_name(g->driver));
        fprintf(f, "      \"family\": \"%s\",\n", amds_family_name(g->family));
        fprintf(f, "      \"board_name\": \"%s\",\n", g->board_name);
        fprintf(f, "      \"pci_slot\": \"%s\",\n", g->pci_slot);
        fprintf(f, "      \"pci_device_id\": \"%s\",\n", g->pci_device_id);
        fprintf(f, "      \"memory_type\": \"%s\",\n", g->memory_type);
        fprintf(f, "      \"memory_bus_bits\": %d,\n", g->memory_bus_bits);
        fprintf(f, "      \"memory_chips\": %d,\n", g->memory_chips);
        fprintf(f, "      \"logical_banks\": %d,\n", g->logical_banks);
        fprintf(f, "      \"metrics\": {\n");
        fprintf(f, "        \"sclk_mhz\": %.2f,\n", g->metrics.sclk_mhz);
        fprintf(f, "        \"mclk_mhz\": %.2f,\n", g->metrics.mclk_mhz);
        fprintf(f, "        \"vddc_mv\": %.2f,\n", g->metrics.vddc_mv);
        fprintf(f, "        \"temp_edge_c\": %.2f,\n", g->metrics.temp_edge_c);
        fprintf(f, "        \"temp_hotspot_c\": %.2f,\n", g->metrics.temp_hotspot_c);
        fprintf(f, "        \"power_w\": %.2f,\n", g->metrics.power_w);
        fprintf(f, "        \"power_cap_w\": %.2f,\n", g->metrics.power_cap_w);
        fprintf(f, "        \"gpu_busy_pct\": %.2f,\n", g->metrics.gpu_busy_pct);
        fprintf(f, "        \"mem_busy_pct\": %.2f,\n", g->metrics.mem_busy_pct);
        fprintf(f, "        \"vram_used\": %llu,\n", (unsigned long long)g->metrics.vram_used);
        fprintf(f, "        \"vram_total\": %llu\n", (unsigned long long)g->metrics.vram_total);
        fprintf(f, "      },\n");
        fprintf(f, "      \"ras\": {\n");
        fprintf(f, "        \"available\": %s,\n", g->ras.available ? "true" : "false");
        fprintf(f, "        \"corrected\": %llu,\n", (unsigned long long)g->ras.corrected);
        fprintf(f, "        \"uncorrected\": %llu,\n", (unsigned long long)g->ras.uncorrected);
        fprintf(f, "        \"bad_pages\": %llu\n", (unsigned long long)g->ras.bad_pages);
        fprintf(f, "      }\n");
        fprintf(f, "    }%s\n", i == gpu_count - 1 ? "" : ",");
    }

    fprintf(f, "  ]\n");
    fprintf(f, "}\n");
    fclose(f);
    return 0;
}

int amds_export_csv(const amds_config_t *cfg, amds_gpu_t *gpus, int gpu_count) {
    if (ensure_parent_dirs(cfg) < 0) return -1;

    char path[4096];
    snprintf(path, sizeof(path), "%s/telemetry.csv", cfg->csv_dir);

    FILE *f = fopen(path, "w");
    if (!f) return -1;

    fprintf(f, "gpu_index,drm_name,driver,family,board_name,pci_slot,pci_device_id,memory_type,memory_bus_bits,memory_chips,logical_banks,sclk_mhz,mclk_mhz,vddc_mv,temp_edge_c,temp_hotspot_c,power_w,power_cap_w,gpu_busy_pct,mem_busy_pct,vram_used,vram_total,ras_available,ras_corrected,ras_uncorrected,ras_bad_pages\n");

    for (int i = 0; i < gpu_count; i++) {
        amds_gpu_t *g = &gpus[i];
        fprintf(f,
                "%d,%s,%s,%s,%s,%s,%s,%s,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%llu,%llu,%d,%llu,%llu,%llu\n",
                g->index,
                g->drm_name,
                amds_driver_name(g->driver),
                amds_family_name(g->family),
                g->board_name,
                g->pci_slot,
                g->pci_device_id,
                g->memory_type,
                g->memory_bus_bits,
                g->memory_chips,
                g->logical_banks,
                g->metrics.sclk_mhz,
                g->metrics.mclk_mhz,
                g->metrics.vddc_mv,
                g->metrics.temp_edge_c,
                g->metrics.temp_hotspot_c,
                g->metrics.power_w,
                g->metrics.power_cap_w,
                g->metrics.gpu_busy_pct,
                g->metrics.mem_busy_pct,
                (unsigned long long)g->metrics.vram_used,
                (unsigned long long)g->metrics.vram_total,
                g->ras.available ? 1 : 0,
                (unsigned long long)g->ras.corrected,
                (unsigned long long)g->ras.uncorrected,
                (unsigned long long)g->ras.bad_pages
        );
    }

    fclose(f);
    return 0;
}

int amds_export_report(const amds_config_t *cfg, amds_gpu_t *gpus, int gpu_count) {
    if (ensure_parent_dirs(cfg) < 0) return -1;

    FILE *f = fopen(cfg->report_path, "w");
    if (!f) return -1;

    fprintf(f, "# AMDS Report\n\n");
    fprintf(f, "Generated at UNIX time `%ld`.\n\n", (long)time(NULL));
    fprintf(f, "Version: `%s`\n\n", amds_version_string());

    for (int i = 0; i < gpu_count; i++) {
        amds_analysis_t a;
        amds_analyze_gpu(&gpus[i], &a);

        fprintf(f, "## GPU%d — %s\n\n", gpus[i].index, gpus[i].board_name[0] ? gpus[i].board_name : gpus[i].drm_name);
        fprintf(f, "- DRM name: `%s`\n", gpus[i].drm_name);
        fprintf(f, "- Driver: `%s`\n", amds_driver_name(gpus[i].driver));
        fprintf(f, "- Family: `%s`\n", amds_family_name(gpus[i].family));
        fprintf(f, "- PCI slot: `%s`\n", gpus[i].pci_slot);
        fprintf(f, "- PCI device ID: `%s`\n", gpus[i].pci_device_id);
        fprintf(f, "- Memory type: `%s`\n", gpus[i].memory_type);
        fprintf(f, "- Memory bus: `%d-bit`\n", gpus[i].memory_bus_bits);
        fprintf(f, "- Memory chips: `%d`\n", gpus[i].memory_chips);
        fprintf(f, "- Logical banks: `%d`\n", gpus[i].logical_banks);
        fprintf(f, "- Edge temperature: `%.2f C`\n", gpus[i].metrics.temp_edge_c);
        fprintf(f, "- Hotspot temperature: `%.2f C`\n", gpus[i].metrics.temp_hotspot_c);
        fprintf(f, "- Power: `%.2f / %.2f W`\n", gpus[i].metrics.power_w, gpus[i].metrics.power_cap_w);
        fprintf(f, "- GPU busy: `%.2f%%`\n", gpus[i].metrics.gpu_busy_pct);
        fprintf(f, "- Memory busy: `%.2f%%`\n", gpus[i].metrics.mem_busy_pct);
        fprintf(f, "- VRAM used: `%llu`\n", (unsigned long long)gpus[i].metrics.vram_used);
        fprintf(f, "- VRAM total: `%llu`\n", (unsigned long long)gpus[i].metrics.vram_total);
        fprintf(f, "- RAS available: `%s`\n", gpus[i].ras.available ? "true" : "false");
        fprintf(f, "- RAS corrected: `%llu`\n", (unsigned long long)gpus[i].ras.corrected);
        fprintf(f, "- RAS uncorrected: `%llu`\n", (unsigned long long)gpus[i].ras.uncorrected);
        fprintf(f, "- RAS bad pages: `%llu`\n\n", (unsigned long long)gpus[i].ras.bad_pages);

        fprintf(f, "### Analysis\n\n");
        fprintf(f, "- Total error signal: `%llu`\n", (unsigned long long)a.total_errors);
        fprintf(f, "- Single-bit-like signal: `%llu`\n", (unsigned long long)a.single_bit_like);
        fprintf(f, "- Clustered-like signal: `%llu`\n", (unsigned long long)a.clustered_like);
        fprintf(f, "- Thermal correlation flag: `%llu`\n", (unsigned long long)a.thermal_correlated);
        fprintf(f, "- Voltage correlation flag: `%llu`\n", (unsigned long long)a.voltage_correlated);
        fprintf(f, "- Recommendation: %s\n\n", a.recommendation);
    }

    fclose(f);
    return 0;
}