#ifndef AMDS_CLI_H
#define AMDS_CLI_H

#include "amds_config.h"
#include "amds_gpu.h"

int amds_run_cli(const amds_config_t *cfg, amds_gpu_t *gpus, int gpu_count);

#endif