#include "../include/amds.h"
#include <CL/cl.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

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
    if (g_amds_logger) amds_log_printf(g_amds_logger, "[STRESS] starting FP32 stress on GPU%d", gpu->index);
    if (!ctx || !ctx->ready) return -1;
    if (ensure_stress_program(ctx) < 0) return -1;

    cl_int err;
    cl_context context = (cl_context)ctx->context;
    cl_command_queue q = (cl_command_queue)ctx->queue;
    cl_kernel k = (cl_kernel)ctx->k_fp32;

    size_t elems = 1 << 20;
    size_t bytes = elems * sizeof(float) * 4;

    if (!ctx->buf_stress) {
        ctx->buf_stress = clCreateBuffer(context, CL_MEM_READ_WRITE, bytes, NULL, &err);
        if (err != CL_SUCCESS) {
            if (g_amds_logger) amds_log_printf(g_amds_logger, "[STRESS] failed to create stress buffer: %d", err);
            return -1;
        }
    }

    uint32_t total_loops = 1000000;
    uint32_t chunk_size = 50000;
    cl_mem buf = (cl_mem)ctx->buf_stress;
    clSetKernelArg(k, 0, sizeof(cl_mem), &buf);
    clSetKernelArg(k, 1, sizeof(uint32_t), &chunk_size);

    size_t global = elems;
    for (uint32_t i = 0; i < total_loops / chunk_size; i++) {
        err = clEnqueueNDRangeKernel(q, k, 1, NULL, &global, NULL, 0, NULL, NULL);
        if (err != CL_SUCCESS) {
            if (g_amds_logger) amds_log_printf(g_amds_logger, "[STRESS] kernel launch failed at chunk %u: %d", i, err);
            return -1;
        }
        clFlush(q);
    }

    clFinish(q);
    log_stress(gpu, lg, "CORE_FP32");
    return 0;
}

int amds_core_stress_fp64(amds_gpu_t *gpu, amds_ocl_ctx_t *ctx, amds_logger_t *lg) {
    if (g_amds_logger) amds_log_printf(g_amds_logger, "[STRESS] starting FP64 stress on GPU%d", gpu->index);
    if (!ctx || !ctx->ready || !ctx->has_fp64) return -1;
    if (ensure_stress_program(ctx) < 0) return -1;
    if (!ctx->k_fp64) return -1;

    cl_int err;
    cl_context context = (cl_context)ctx->context;
    cl_command_queue q = (cl_command_queue)ctx->queue;
    cl_kernel k = (cl_kernel)ctx->k_fp64;

    size_t elems = 1 << 20;
    size_t bytes = elems * sizeof(double) * 2;

    if (!ctx->buf_stress) {
        ctx->buf_stress = clCreateBuffer(context, CL_MEM_READ_WRITE, bytes, NULL, &err);
        if (err != CL_SUCCESS) {
            if (g_amds_logger) amds_log_printf(g_amds_logger, "[STRESS] failed to create stress buffer: %d", err);
            return -1;
        }
    }

    uint32_t total_loops = 300000;
    uint32_t chunk_size = 15000;
    cl_mem buf = (cl_mem)ctx->buf_stress;
    clSetKernelArg(k, 0, sizeof(cl_mem), &buf);
    clSetKernelArg(k, 1, sizeof(uint32_t), &chunk_size);

    size_t global = elems;
    for (uint32_t i = 0; i < total_loops / chunk_size; i++) {
        err = clEnqueueNDRangeKernel(q, k, 1, NULL, &global, NULL, 0, NULL, NULL);
        if (err != CL_SUCCESS) {
            if (g_amds_logger) amds_log_printf(g_amds_logger, "[STRESS] kernel launch failed at chunk %u: %d", i, err);
            return -1;
        }
        clFlush(q);
    }

    clFinish(q);
    log_stress(gpu, lg, "CORE_FP64");
    return 0;
}