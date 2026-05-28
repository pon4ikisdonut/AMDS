#include "../include/amds.h"
#include <CL/cl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

static const char *amds_vram_src =
"typedef struct { ulong addr; uint expected; uint actual; } err_t;\n"
"__kernel void fill_pattern(__global uint *buf, uint pattern, ulong words) {\n"
"  ulong i = get_global_id(0); if (i < words) buf[i] = pattern;\n"
"}\n"
"__kernel void verify_pattern(__global uint *buf, uint pattern, ulong words, __global uint *err_count, __global err_t *errs) {\n"
"  ulong i = get_global_id(0); if (i >= words) return; uint v = buf[i];\n"
"  if (v != pattern) { uint idx = atomic_inc(err_count); if (idx < 2048) { errs[idx].addr = i * 4UL; errs[idx].expected = pattern; errs[idx].actual = v; } }\n"
"}\n"
"__kernel void fill_lcg(__global uint *buf, uint seed, ulong words) {\n"
"  ulong i = get_global_id(0); if (i >= words) return; uint x = (uint)i ^ seed; x = x * 1664525u + 1013904223u; buf[i] = x;\n"
"}\n"
"__kernel void verify_lcg(__global uint *buf, uint seed, ulong words, __global uint *err_count, __global err_t *errs) {\n"
"  ulong i = get_global_id(0); if (i >= words) return; uint x = (uint)i ^ seed; x = x * 1664525u + 1013904223u; uint v = buf[i];\n"
"  if (v != x) { uint idx = atomic_inc(err_count); if (idx < 2048) { errs[idx].addr = i * 4UL; errs[idx].expected = x; errs[idx].actual = v; } }\n"
"}\n";

static void log_vram_err(amds_gpu_t *gpu, amds_logger_t *lg, const char *stage, const amds_ocl_err_t *e) {
    amds_error_loc_t loc;
    amds_decode_address(gpu, e->addr, &loc);

    char line[1024];
    snprintf(line, sizeof(line),
             "[%ld] [%s] [GPU%d %s %s EDGE=%.1f HOT=%.1f PWR=%.1f GPU=%.0f%% MEM=%.0f%%] "
             "[%s] [ADDR=0x%016llx: %08x vs %08x]",
             (long)time(NULL),
             stage,
             gpu->index,
             gpu->drm_name,
             amds_family_name(gpu->family),
             gpu->metrics.temp_edge_c,
             gpu->metrics.temp_hotspot_c,
             gpu->metrics.power_w,
             gpu->metrics.gpu_busy_pct,
             gpu->metrics.mem_busy_pct,
             loc.label,
             (unsigned long long)e->addr,
             e->expected,
             e->actual);
    amds_log_text(lg, line);
}

static int pick_amd_gpu(cl_platform_id *out_p, cl_device_id *out_d) {
    cl_uint pcount = 0;
    if (clGetPlatformIDs(0, NULL, &pcount) != CL_SUCCESS || !pcount) return -1;

    cl_platform_id *plats = calloc(pcount, sizeof(*plats));
    if (!plats) return -1;
    clGetPlatformIDs(pcount, plats, NULL);

    for (cl_uint p = 0; p < pcount; p++) {
        cl_uint dcount = 0;
        if (clGetDeviceIDs(plats[p], CL_DEVICE_TYPE_GPU, 0, NULL, &dcount) != CL_SUCCESS || !dcount) continue;

        cl_device_id *devs = calloc(dcount, sizeof(*devs));
        if (!devs) continue;
        clGetDeviceIDs(plats[p], CL_DEVICE_TYPE_GPU, dcount, devs, NULL);

        for (cl_uint d = 0; d < dcount; d++) {
            char vendor[256] = {0};
            clGetDeviceInfo(devs[d], CL_DEVICE_VENDOR, sizeof(vendor), vendor, NULL);
            if (strstr(vendor, "AMD") || strstr(vendor, "Advanced Micro Devices")) {
                *out_p = plats[p];
                *out_d = devs[d];
                free(devs);
                free(plats);
                return 0;
            }
        }
        free(devs);
    }

    free(plats);
    return -1;
}

int amds_ocl_init(amds_ocl_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));

    cl_platform_id platform = NULL;
    cl_device_id device = NULL;
    if (pick_amd_gpu(&platform, &device) < 0) return -1;

    cl_int err;
    cl_context context = clCreateContext(NULL, 1, &device, NULL, NULL, &err);
    if (!context || err != CL_SUCCESS) return -1;

    cl_command_queue queue = clCreateCommandQueue(context, device, 0, &err);
    if (!queue || err != CL_SUCCESS) {
        clReleaseContext(context);
        return -1;
    }

    const char *src[] = { amds_vram_src };
    cl_program prog = clCreateProgramWithSource(context, 1, src, NULL, &err);
    if (err != CL_SUCCESS) {
        clReleaseCommandQueue(queue);
        clReleaseContext(context);
        return -1;
    }

    err = clBuildProgram(prog, 1, &device, "", NULL, NULL);
    if (err != CL_SUCCESS) {
        clReleaseProgram(prog);
        clReleaseCommandQueue(queue);
        clReleaseContext(context);
        return -1;
    }

    cl_kernel k_fill_pattern = clCreateKernel(prog, "fill_pattern", &err);
    if (err != CL_SUCCESS) goto fail;
    cl_kernel k_verify_pattern = clCreateKernel(prog, "verify_pattern", &err);
    if (err != CL_SUCCESS) goto fail;
    cl_kernel k_fill_lcg = clCreateKernel(prog, "fill_lcg", &err);
    if (err != CL_SUCCESS) goto fail;
    cl_kernel k_verify_lcg = clCreateKernel(prog, "verify_lcg", &err);
    if (err != CL_SUCCESS) goto fail;

    cl_ulong global_mem = 0;
    clGetDeviceInfo(device, CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(global_mem), &global_mem, NULL);

    size_t vram_bytes = (size_t)((global_mem > (512ULL << 20)) ? (global_mem / 4) : (256ULL << 20));
    size_t vram_words = vram_bytes / 4;

    cl_mem buf_vram = clCreateBuffer(context, CL_MEM_READ_WRITE, vram_bytes, NULL, &err);
    if (err != CL_SUCCESS) goto fail;
    cl_mem buf_err_count = clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(uint32_t), NULL, &err);
    if (err != CL_SUCCESS) goto fail;
    cl_mem buf_errs = clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(amds_ocl_err_t) * 2048, NULL, &err);
    if (err != CL_SUCCESS) goto fail;

    char ext[8192] = {0};
    clGetDeviceInfo(device, CL_DEVICE_EXTENSIONS, sizeof(ext), ext, NULL);

    ctx->platform = platform;
    ctx->device = device;
    ctx->context = context;
    ctx->queue = queue;
    ctx->program_vram = prog;
    ctx->k_fill_pattern = k_fill_pattern;
    ctx->k_verify_pattern = k_verify_pattern;
    ctx->k_fill_lcg = k_fill_lcg;
    ctx->k_verify_lcg = k_verify_lcg;
    ctx->buf_vram = buf_vram;
    ctx->buf_err_count = buf_err_count;
    ctx->buf_errs = buf_errs;
    ctx->vram_bytes = vram_bytes;
    ctx->vram_words = vram_words;
    ctx->has_fp64 = strstr(ext, "cl_khr_fp64") != NULL;
    ctx->ready = true;
    return 0;

fail:
    if (ctx->buf_stress) clReleaseMemObject((cl_mem)ctx->buf_stress);
    if (ctx->buf_errs) clReleaseMemObject((cl_mem)ctx->buf_errs);
    if (ctx->buf_err_count) clReleaseMemObject((cl_mem)ctx->buf_err_count);
    if (ctx->buf_vram) clReleaseMemObject((cl_mem)ctx->buf_vram);
    if (k_verify_lcg) clReleaseKernel(k_verify_lcg);
    if (k_fill_lcg) clReleaseKernel(k_fill_lcg);
    if (k_verify_pattern) clReleaseKernel(k_verify_pattern);
    if (k_fill_pattern) clReleaseKernel(k_fill_pattern);
    if (prog) clReleaseProgram(prog);
    if (queue) clReleaseCommandQueue(queue);
    if (context) clReleaseContext(context);
    memset(ctx, 0, sizeof(*ctx));
    return -1;
}

void amds_ocl_close(amds_ocl_ctx_t *ctx) {
    if (ctx->buf_stress) clReleaseMemObject((cl_mem)ctx->buf_stress);
    if (ctx->buf_errs) clReleaseMemObject((cl_mem)ctx->buf_errs);
    if (ctx->buf_err_count) clReleaseMemObject((cl_mem)ctx->buf_err_count);
    if (ctx->buf_vram) clReleaseMemObject((cl_mem)ctx->buf_vram);
    if (ctx->k_fp64) clReleaseKernel((cl_kernel)ctx->k_fp64);
    if (ctx->k_fp32) clReleaseKernel((cl_kernel)ctx->k_fp32);
    if (ctx->k_verify_lcg) clReleaseKernel((cl_kernel)ctx->k_verify_lcg);
    if (ctx->k_fill_lcg) clReleaseKernel((cl_kernel)ctx->k_fill_lcg);
    if (ctx->k_verify_pattern) clReleaseKernel((cl_kernel)ctx->k_verify_pattern);
    if (ctx->k_fill_pattern) clReleaseKernel((cl_kernel)ctx->k_fill_pattern);
    if (ctx->program_stress) clReleaseProgram((cl_program)ctx->program_stress);
    if (ctx->program_vram) clReleaseProgram((cl_program)ctx->program_vram);
    if (ctx->queue) clReleaseCommandQueue((cl_command_queue)ctx->queue);
    if (ctx->context) clReleaseContext((cl_context)ctx->context);
    memset(ctx, 0, sizeof(*ctx));
}

static int read_and_log_errors(amds_gpu_t *gpu, amds_ocl_ctx_t *ctx, amds_logger_t *lg, const char *stage, uint32_t *err_count_out) {
    cl_command_queue q = (cl_command_queue)ctx->queue;
    cl_mem err_count_buf = (cl_mem)ctx->buf_err_count;
    cl_mem errs_buf = (cl_mem)ctx->buf_errs;

    uint32_t err_count = 0;
    amds_ocl_err_t errs[2048];

    clEnqueueReadBuffer(q, err_count_buf, CL_TRUE, 0, sizeof(err_count), &err_count, 0, NULL, NULL);
    if (err_count_out) *err_count_out = err_count;
    if (!err_count) return 0;

    if (err_count > 2048) err_count = 2048;
    clEnqueueReadBuffer(q, errs_buf, CL_TRUE, 0, sizeof(amds_ocl_err_t) * err_count, errs, 0, NULL, NULL);

    for (uint32_t i = 0; i < err_count; i++) log_vram_err(gpu, lg, stage, &errs[i]);
    return (int)err_count;
}

static void clear_err_counter(amds_ocl_ctx_t *ctx) {
    cl_command_queue q = (cl_command_queue)ctx->queue;
    cl_mem err_count_buf = (cl_mem)ctx->buf_err_count;
    uint32_t zero = 0;
    clEnqueueWriteBuffer(q, err_count_buf, CL_TRUE, 0, sizeof(zero), &zero, 0, NULL, NULL);
}

int amds_vram_test_pattern(amds_gpu_t *gpu, amds_ocl_ctx_t *ctx, amds_logger_t *lg) {
    if (!ctx || !ctx->ready) return -1;

    cl_command_queue q = (cl_command_queue)ctx->queue;
    cl_kernel k_fill = (cl_kernel)ctx->k_fill_pattern;
    cl_kernel k_verify = (cl_kernel)ctx->k_verify_pattern;
    cl_mem buf = (cl_mem)ctx->buf_vram;
    cl_mem err_count = (cl_mem)ctx->buf_err_count;
    cl_mem errs = (cl_mem)ctx->buf_errs;
    cl_ulong words = (cl_ulong)ctx->vram_words;
    size_t global = ((ctx->vram_words + 255) / 256) * 256;

    uint32_t patterns[] = {0x55555555u, 0xAAAAAAAAu, 0xFFFFFFFFu, 0x00000000u};

    for (int i = 0; i < 4; i++) {
        uint32_t p = patterns[i];
        clear_err_counter(ctx);

        clSetKernelArg(k_fill, 0, sizeof(cl_mem), &buf);
        clSetKernelArg(k_fill, 1, sizeof(uint32_t), &p);
        clSetKernelArg(k_fill, 2, sizeof(cl_ulong), &words);
        clEnqueueNDRangeKernel(q, k_fill, 1, NULL, &global, NULL, 0, NULL, NULL);

        clSetKernelArg(k_verify, 0, sizeof(cl_mem), &buf);
        clSetKernelArg(k_verify, 1, sizeof(uint32_t), &p);
        clSetKernelArg(k_verify, 2, sizeof(cl_ulong), &words);
        clSetKernelArg(k_verify, 3, sizeof(cl_mem), &err_count);
        clSetKernelArg(k_verify, 4, sizeof(cl_mem), &errs);
        clEnqueueNDRangeKernel(q, k_verify, 1, NULL, &global, NULL, 0, NULL, NULL);
        clFinish(q);

        uint32_t ec = 0;
        int got = read_and_log_errors(gpu, ctx, lg, "VRAM_PATTERN", &ec);
        if (got > 0) return got;
    }

    return 0;
}

int amds_vram_test_walking(amds_gpu_t *gpu, amds_ocl_ctx_t *ctx, amds_logger_t *lg) {
    if (!ctx || !ctx->ready) return -1;

    cl_command_queue q = (cl_command_queue)ctx->queue;
    cl_kernel k_fill = (cl_kernel)ctx->k_fill_pattern;
    cl_kernel k_verify = (cl_kernel)ctx->k_verify_pattern;
    cl_mem buf = (cl_mem)ctx->buf_vram;
    cl_mem err_count = (cl_mem)ctx->buf_err_count;
    cl_mem errs = (cl_mem)ctx->buf_errs;
    cl_ulong words = (cl_ulong)ctx->vram_words;
    size_t global = ((ctx->vram_words + 255) / 256) * 256;

    for (int b = 0; b < 32; b++) {
        uint32_t p = 1u << b;
        clear_err_counter(ctx);

        clSetKernelArg(k_fill, 0, sizeof(cl_mem), &buf);
        clSetKernelArg(k_fill, 1, sizeof(uint32_t), &p);
        clSetKernelArg(k_fill, 2, sizeof(cl_ulong), &words);
        clEnqueueNDRangeKernel(q, k_fill, 1, NULL, &global, NULL, 0, NULL, NULL);

        clSetKernelArg(k_verify, 0, sizeof(cl_mem), &buf);
        clSetKernelArg(k_verify, 1, sizeof(uint32_t), &p);
        clSetKernelArg(k_verify, 2, sizeof(cl_ulong), &words);
        clSetKernelArg(k_verify, 3, sizeof(cl_mem), &err_count);
        clSetKernelArg(k_verify, 4, sizeof(cl_mem), &errs);
        clEnqueueNDRangeKernel(q, k_verify, 1, NULL, &global, NULL, 0, NULL, NULL);
        clFinish(q);

        uint32_t ec = 0;
        int got = read_and_log_errors(gpu, ctx, lg, "VRAM_WALKING", &ec);
        if (got > 0) return got;
    }

    return 0;
}

int amds_vram_test_prng(amds_gpu_t *gpu, amds_ocl_ctx_t *ctx, amds_logger_t *lg) {
    if (!ctx || !ctx->ready) return -1;

    cl_command_queue q = (cl_command_queue)ctx->queue;
    cl_kernel k_fill = (cl_kernel)ctx->k_fill_lcg;
    cl_kernel k_verify = (cl_kernel)ctx->k_verify_lcg;
    cl_mem buf = (cl_mem)ctx->buf_vram;
    cl_mem err_count = (cl_mem)ctx->buf_err_count;
    cl_mem errs = (cl_mem)ctx->buf_errs;
    cl_ulong words = (cl_ulong)ctx->vram_words;
    size_t global = ((ctx->vram_words + 255) / 256) * 256;
    uint32_t seed = (uint32_t)time(NULL);

    clear_err_counter(ctx);

    clSetKernelArg(k_fill, 0, sizeof(cl_mem), &buf);
    clSetKernelArg(k_fill, 1, sizeof(uint32_t), &seed);
    clSetKernelArg(k_fill, 2, sizeof(cl_ulong), &words);
    clEnqueueNDRangeKernel(q, k_fill, 1, NULL, &global, NULL, 0, NULL, NULL);

    clSetKernelArg(k_verify, 0, sizeof(cl_mem), &buf);
    clSetKernelArg(k_verify, 1, sizeof(uint32_t), &seed);
    clSetKernelArg(k_verify, 2, sizeof(cl_ulong), &words);
    clSetKernelArg(k_verify, 3, sizeof(cl_mem), &err_count);
    clSetKernelArg(k_verify, 4, sizeof(cl_mem), &errs);
    clEnqueueNDRangeKernel(q, k_verify, 1, NULL, &global, NULL, 0, NULL, NULL);
    clFinish(q);

    uint32_t ec = 0;
    return read_and_log_errors(gpu, ctx, lg, "VRAM_PRNG", &ec);
}