CC=gcc
CFLAGS=-O2 -pipe -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -DCL_TARGET_OPENCL_VERSION=120 -Iinclude
LDFLAGS=-lncursesw -lOpenCL -lpthread -lm

SRC=src/main.c src/version.c src/config.c src/logger.c src/gpu_discovery.c src/gpu_sysfs.c src/gpu_ras.c src/cli.c src/tui.c src/util.c src/vram_opencl.c src/stress_opencl.c src/export.c src/analysis.c
OBJ=$(SRC:.c=.o)

all: amds

amds: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

clean:
	rm -f $(OBJ)

clear: clean
	rm -f amds amds_diag.log
	rm -rf exports

rebuild: clear all

run: amds
	./amds