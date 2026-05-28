#ifndef AMDS_EXPORT_H
#define AMDS_EXPORT_H

#include "amds_gpu.h"
#include "amds_config.h"

int amds_export_json(const amds_config_t *cfg, amds_gpu_t *gpus, int gpu_count);
int amds_export_csv(const amds_config_t *cfg, amds_gpu_t *gpus, int gpu_count);
int amds_export_report(const amds_config_t *cfg, amds_gpu_t *gpus, int gpu_count);

#endif