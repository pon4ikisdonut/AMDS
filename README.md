# AMDS v0.2.1

AMDS is an AMD GPU diagnostics suite for Linux, written in C.

It is designed for:

- multi-GPU monitoring
- VRAM diagnostics
- GPU core stress testing
- RAS error ingestion
- crash-resistant logging
- ncurses TUI
- CLI automation
- JSON/CSV/report export
- heuristic fault localization by GPU family

## Scope

AMDS uses Linux interfaces that are publicly available and practical for diagnostics:

- `sysfs`
- `hwmon`
- `amdgpu` telemetry files
- `amdgpu` RAS sysfs nodes
- OpenCL for VRAM/core stress

AMDS does **not** claim a universal exact physical DRAM bank/row/column decoder for all AMD generations.

Where exact mapping is not publicly available, AMDS uses logical family-specific heuristics.

## Current feature set

- AMD GPU discovery through `/sys/class/drm`
- `radeon` / `amdgpu` driver detection
- one-second telemetry polling
- RAS bad page and error counter ingestion
- CLI mode
- ncurses TUI mode
- OpenCL VRAM tests:
  - pattern
  - walking bits
  - PRNG
- OpenCL core stress:
  - FP32
  - FP64 when `cl_khr_fp64` is available
- unbuffered log flushing using `fflush()` and `fsync()`
- export to JSON, CSV, and Markdown report
- English-only output files and UI strings

## Build

```bash
sudo apt update
sudo apt install build-essential libncursesw5-dev ocl-icd-opencl-dev opencl-headers clinfo
make
```

## Run

TUI:

```bash
sudo ./amds
```

CLI monitor:

```bash
sudo ./amds --cli --mode monitor --duration 30
```

CLI VRAM tests:

```bash
sudo ./amds --cli --mode vram
```

CLI core stress:

```bash
sudo ./amds --cli --mode core
```

CLI full suite:

```bash
sudo ./amds --cli --mode full
```

## TUI controls

- `Left` / `Right` — select GPU
- `Up` / `Down` — move in control menu
- `Enter` — activate selected menu item
- `r` — refresh now
- `q` — quit

## Modes

### monitor

Only telemetry and RAS polling.

### vram

Runs:

- pattern test
- walking bits
- PRNG sequence test

### core

Runs:

- FP32 compute stress
- FP64 compute stress if the OpenCL device supports `cl_khr_fp64`

### full

Runs all VRAM tests and then core stress.

## Polaris mapping

For Polaris-family boards, AMDS uses a dedicated burst-slot remap model to cluster failures into logical chips/channels rather than a flat modulo map.

This is intended as a practical repair-oriented heuristic for common 8-chip Polaris layouts.

## Logging

The program writes to:

```text
./amds_diag.log
```

Each line is flushed immediately with:

- `fflush()`
- `fsync()`

This is done to preserve as much telemetry and failure evidence as possible during hard hangs.

## Exports

On exit, AMDS writes:

- `exports/amds.json`
- `exports/telemetry.csv`
- `exports/report.md`

## Safety

This software can heavily stress GPUs and may freeze unstable hardware or the full system.

Use it at your own risk.

:3