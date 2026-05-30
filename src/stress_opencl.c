#define _DEFAULT_SOURCE
#include "../include/amds.h"
#include <CL/cl.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>

static const char *amds_stress_src =
"#pragma OPENCL EXTENSION cl_khr_fp64 : enable\n"
"__kernel void fp32_stress(__global float4 *buf, uint loops) {\n"
"  size_t i = get_global_id(0);\n"
"  float4 x = (float4)(i*0.001f, i*0.002f, i*0.003f, i*0.004f);\n"
"  for (uint k = 0; k < loops; k++) {\n"
"    x = native_sin(x) * 1.000123f + native_cos(x) * 0.99991f;\n"
"    x = mad(x, (float4)(1.00001f), (float4)(0.0001f));\n"
"  }\n"
"  buf[i] = x;\n"
"}\n"
"__kernel void fp64_stress(__global double2 *buf, uint loops) {\n"
"  size_t i = get_global_id(0);\n"
"  double2 x = (double2)(i*0.0001, i*0.0002);\n"
"  for (uint k = 0; k < loops; k++) {\n"
"    x = sin(x) * 1.0000001 + cos(x) * 0.9999991;\n"
"    x = x * 1.00000001 + (double2)(0.0000001, 0.0000002);\n"
"  }\n"
"  buf[i] = x;\n"
"}\n";

static int ensure_stress_program(amds_ocl_ctx_t *ctx) {
    if (ctx->program_stress && ctx->k_fp32) return 0;

    if (g_amds_logger) amds_log_printf(g_amds_logger, "[STRESS] building stress kernels");

    cl_int err;
    cl_context context = (cl_context)ctx->context;
    cl_device_id device = (cl_device_id)ctx->device;

    const char *src[] = { amds_stress_src };
    cl_program prog = clCreateProgramWithSource(context, 1, src, NULL, &err);
    if (err != CL_SUCCESS) {
        if (g_amds_logger) amds_log_printf(g_amds_logger, "[STRESS] failed to create program: %d", err);
        return -1;
    }

    err = clBuildProgram(prog, 1, &device, "", NULL, NULL);
    if (err != CL_SUCCESS) {
        if (g_amds_logger) {
            char log[16384];
            clGetProgramBuildInfo(prog, device, CL_PROGRAM_BUILD_LOG, sizeof(log), log, NULL);
            amds_log_printf(g_amds_logger, "[STRESS] program build failed: %d\n%s", err, log);
        }
        clReleaseProgram(prog);
        return -1;
    }

    cl_kernel k_fp32 = clCreateKernel(prog, "fp32_stress", &err);
    if (err != CL_SUCCESS) {
        if (g_amds_logger) amds_log_printf(g_amds_logger, "[STRESS] failed to create fp32 kernel");
        clReleaseProgram(prog);
        return -1;
    }

    cl_kernel k_fp64 = NULL;
    if (ctx->has_fp64) {
        k_fp64 = clCreateKernel(prog, "fp64_stress", &err);
        if (err != CL_SUCCESS) {
            if (g_amds_logger) amds_log_printf(g_amds_logger, "[STRESS] failed to create fp64 kernel");
            k_fp64 = NULL;
        }
    }

    ctx->program_stress = prog;
    ctx->k_fp32 = k_fp32;
    ctx->k_fp64 = k_fp64;
    if (g_amds_logger) amds_log_printf(g_amds_logger, "[STRESS] kernels built successfully");
    return 0;
}

static pthread_t g_kmsg_thread;
static volatile bool g_kmsg_run = false;
static amds_logger_t *g_kmsg_lg = NULL;

static void *kmsg_monitor_thread(void *arg) {
    (void)arg;
    int fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK);
    if (fd < 0) return NULL;

    lseek(fd, 0, SEEK_END);
    char buf[2048];

    while (g_kmsg_run) {
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = 0;
            bool hang = strstr(buf, "ring gfx timeout") != NULL;
            bool failed = strstr(buf, "GPU Recovery Failed") != NULL;
            bool lost = strstr(buf, "VRAM is lost") != NULL;

            if (hang || failed || lost) {
                char msg[1024];
                snprintf(msg, sizeof(msg), "[KMSG_ALERT] %s%s%s", 
                         hang ? "GPU_HANG " : "",
                         failed ? "RECOVERY_FAILED " : "",
                         lost ? "VRAM_LOST " : "");
                amds_log_text(g_kmsg_lg, msg);
                if (g_kmsg_lg && g_kmsg_lg->fp) fflush(g_kmsg_lg->fp);
                if (g_amds_logger) amds_log_printf(g_amds_logger, "[KMSG] DETECTED: %s", msg);
            }
        }
        usleep(100000);
    }
    close(fd);
    return NULL;
}

int amds_kmsg_monitor_start(amds_logger_t *lg) {
    if (g_kmsg_run) return 0;
    g_kmsg_lg = lg;
    g_kmsg_run = true;
    if (pthread_create(&g_kmsg_thread, NULL, kmsg_monitor_thread, NULL) != 0) {
        g_kmsg_run = false;
        return -1;
    }
    return 0;
}

void amds_kmsg_monitor_stop(void) {
    if (!g_kmsg_run) return;
    g_kmsg_run = false;
    pthread_join(g_kmsg_thread, NULL);
}

static void log_stress(amds_gpu_t *gpu, amds_logger_t *lg, const char *stage) {
    if (g_amds_logger) {
        amds_log_printf(g_amds_logger, "[STRESS] %s finished for GPU%d (%s)", stage, gpu->index, gpu->drm_name);
    }
    char line[1024];
    snprintf(line, sizeof(line),
             "[%ld] [%s] [GPU%d %s %s SCLK=%.0f MCLK=%.0f EDGE=%.1f HOT=%.1f PWR=%.1f GPU=%.0f%% MEM=%.0f%%]",
             (long)time(NULL),
             stage,
             gpu->index,
             gpu->drm_name,
             amds_family_name(gpu->family),
             gpu->metrics.sclk_mhz,
             gpu->metrics.mclk_mhz,
             gpu->metrics.temp_edge_c,
             gpu->metrics.temp_hotspot_c,
             gpu->metrics.power_w,
             gpu->metrics.gpu_busy_pct,
             gpu->metrics.mem_busy_pct);
    amds_log_text(lg, line);
}

int amds_core_stress_fp32(amds_gpu_t *gpu, amds_ocl_ctx_t *ctx, amds_logger_t *lg) {
    char lock_path[512];
    snprintf(lock_path, sizeof(lock_path), "/tmp/amds_gpu%d_fp32.pending", gpu->index);

    if (access(lock_path, F_OK) == 0) {
        if (g_amds_logger) amds_log_printf(g_amds_logger, "[STRESS] GPU%d FP32: CRASH_DETECTED (lockfile exists)", gpu->index);
        amds_log_text(lg, "[CORE_FP32] CRASH_DETECTED: Previous run failed to finish");
    }

    FILE *lf = fopen(lock_path, "w");
    if (lf) { fprintf(lf, "%d", (int)time(NULL)); fclose(lf); }

    if (g_amds_logger) {
        amds_log_printf(g_amds_logger, "[STRESS] starting FP32 stress on GPU%d", gpu->index);
        if (g_amds_logger->fp) fflush(g_amds_logger->fp);
    }
    
    char start_msg[256];
    snprintf(start_msg, sizeof(start_msg), "[CORE_FP32_BEGIN] GPU%d starting stress test", gpu->index);
    amds_log_text(lg, start_msg);
    if (lg && lg->fp) fflush(lg->fp);

    if (!ctx || !ctx->ready) {
        unlink(lock_path);
        return -1;
    }
    if (ensure_stress_program(ctx) < 0) {
        unlink(lock_path);
        return -1;
    }

    cl_int err;
    cl_context context = (cl_context)ctx->context;
    cl_command_queue q = (cl_command_queue)ctx->queue;
    cl_kernel k = (cl_kernel)ctx->k_fp32;

    size_t elems = 1 << 16;
    size_t bytes = elems * sizeof(float) * 4;

    if (ctx->buf_stress) {
        clReleaseMemObject((cl_mem)ctx->buf_stress);
        ctx->buf_stress = NULL;
    }

    ctx->buf_stress = clCreateBuffer(context, CL_MEM_READ_WRITE, bytes, NULL, &err);
    if (err != CL_SUCCESS) {
        if (g_amds_logger) amds_log_printf(g_amds_logger, "[STRESS] failed to create stress buffer: %d", err);
        unlink(lock_path);
        return -1;
    }

    uint32_t total_loops = 10000000;
    uint32_t chunk_size = 5000;
    cl_mem buf = (cl_mem)ctx->buf_stress;
    clSetKernelArg(k, 0, sizeof(cl_mem), &buf);
    clSetKernelArg(k, 1, sizeof(uint32_t), &chunk_size);

    size_t global = elems;
    for (uint32_t i = 0; i < total_loops / chunk_size; i++) {
        err = clEnqueueNDRangeKernel(q, k, 1, NULL, &global, NULL, 0, NULL, NULL);
        if (err != CL_SUCCESS) {
            if (g_amds_logger) amds_log_printf(g_amds_logger, "[STRESS] kernel launch failed at chunk %u: %d", i, err);
            unlink(lock_path);
            return -1;
        }
        clFlush(q);
    }

    clFinish(q);
    log_stress(gpu, lg, "CORE_FP32");
    unlink(lock_path);
    return 0;
}

int amds_core_stress_fp64(amds_gpu_t *gpu, amds_ocl_ctx_t *ctx, amds_logger_t *lg) {
    char lock_path[512];
    snprintf(lock_path, sizeof(lock_path), "/tmp/amds_gpu%d_fp64.pending", gpu->index);

    if (access(lock_path, F_OK) == 0) {
        if (g_amds_logger) amds_log_printf(g_amds_logger, "[STRESS] GPU%d FP64: CRASH_DETECTED (lockfile exists)", gpu->index);
        amds_log_text(lg, "[CORE_FP64] CRASH_DETECTED: Previous run failed to finish");
    }

    FILE *lf = fopen(lock_path, "w");
    if (lf) { fprintf(lf, "%d", (int)time(NULL)); fclose(lf); }

    if (g_amds_logger) {
        amds_log_printf(g_amds_logger, "[STRESS] starting FP64 stress on GPU%d", gpu->index);
        if (g_amds_logger->fp) fflush(g_amds_logger->fp);
    }
    
    char start_msg[256];
    snprintf(start_msg, sizeof(start_msg), "[CORE_FP64_BEGIN] GPU%d starting stress test", gpu->index);
    amds_log_text(lg, start_msg);
    if (lg && lg->fp) fflush(lg->fp);

    if (!ctx || !ctx->ready || !ctx->has_fp64) {
        unlink(lock_path);
        return -1;
    }
    if (ensure_stress_program(ctx) < 0) {
        unlink(lock_path);
        return -1;
    }
    if (!ctx->k_fp64) {
        unlink(lock_path);
        return -1;
    }

    cl_int err;
    cl_context context = (cl_context)ctx->context;
    cl_command_queue q = (cl_command_queue)ctx->queue;
    cl_kernel k = (cl_kernel)ctx->k_fp64;

    size_t elems = 1 << 16;
    size_t bytes = elems * sizeof(double) * 2;

    if (ctx->buf_stress) {
        clReleaseMemObject((cl_mem)ctx->buf_stress);
        ctx->buf_stress = NULL;
    }

    ctx->buf_stress = clCreateBuffer(context, CL_MEM_READ_WRITE, bytes, NULL, &err);
    if (err != CL_SUCCESS) {
        if (g_amds_logger) amds_log_printf(g_amds_logger, "[STRESS] failed to create stress buffer: %d", err);
        unlink(lock_path);
        return -1;
    }

    uint32_t total_loops = 3000000;
    uint32_t chunk_size = 2000;
    cl_mem buf = (cl_mem)ctx->buf_stress;
    clSetKernelArg(k, 0, sizeof(cl_mem), &buf);
    clSetKernelArg(k, 1, sizeof(uint32_t), &chunk_size);

    size_t global = elems;
    for (uint32_t i = 0; i < total_loops / chunk_size; i++) {
        err = clEnqueueNDRangeKernel(q, k, 1, NULL, &global, NULL, 0, NULL, NULL);
        if (err != CL_SUCCESS) {
            if (g_amds_logger) amds_log_printf(g_amds_logger, "[STRESS] kernel launch failed at chunk %u: %d", i, err);
            unlink(lock_path);
            return -1;
        }
        clFlush(q);
    }

    clFinish(q);
    log_stress(gpu, lg, "CORE_FP64");
    unlink(lock_path);
    return 0;
}