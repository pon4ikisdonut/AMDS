# AMDS v0.2.1

AMDS is an AMD GPU diagnostics suite for Linux, written in C.

It is designed for:

- **Multi-GPU monitoring** (Clocks, Temps, Power, Usage, VRAM)
- **VRAM diagnostics** (Pattern, Walking 1s, Moving Inversions, Random Noise, PRNG)
- **GPU core stress testing** (FP32 and FP64 heavy compute kernels)
- **RAS error ingestion** (Corrected/Uncorrected errors, Bad Pages)
- **Kmsg monitoring** (Capturing driver/hardware errors during tests)
- **Crash-resistant logging** (Synchronous disk writes for post-mortem analysis)
- **ncurses TUI** (Interactive monitoring and control)
- **CLI automation** (Scriptable execution modes)
- **Flexible Export** (JSON, CSV, and Markdown report generation)
- **Heuristic fault localization** by GPU family (Polaris, Vega, Navi, etc.)

## Scope

AMDS uses public Linux interfaces for diagnostics:
- **Sysfs** (`/sys/class/drm/cardX/device/...`) for telemetry and metrics.
- **Hwmon** for temperatures and fan speeds.
- **Debugfs** (optional) for advanced RAS status.
- **OpenCL** for VRAM and Core stress testing.
- **Kmsg** for kernel log monitoring.

## Installation

### Dependencies

- **GCC**, **Make**
- **Ncursesw** (for TUI)
- **OpenCL Headers & Loader** (for stress tests)
- **Amdgpu** driver with compute support

On Ubuntu/Debian:
```bash
sudo apt install build-essential libncursesw5-dev ocl-icd-opencl-dev
```

### Build

```bash
make
```

## Usage

### TUI Mode (Interactive)

Launch the interactive interface:
```bash
sudo ./amds
```
*(Root privileges are required to access some sysfs/debugfs nodes and for kmsg monitoring).*

### CLI Mode (Automation)

Run a specific diagnostic mode:
```bash
sudo ./amds --cli --mode [monitor|vram|core|full] --duration 60
```

#### CLI Arguments:

- `--cli`: Enable CLI mode (disables TUI).
- `--mode <mode>`: Diagnostic mode:
    - `monitor`: Continuous telemetry monitoring (default).
    - `vram`: Run VRAM pattern and noise tests.
    - `core`: Run heavy FP32/FP64 compute kernels.
    - `full`: Combined stress test (5 passes of VRAM + Core).
- `--duration <sec>`: Duration for monitor mode (default: 60s).
- `--poll-ms <ms>`: Telemetry polling interval (default: 1000ms).
- `--gpu <id>`: Filter by GPU index or "all" (default: all).
- `--json <path>`: Path for JSON telemetry export.
- `--csv-dir <dir>`: Directory for CSV telemetry export.
- `--report <path>`: Path for the final Markdown report.
- `--log <path>`: Path for the diagnostic log.
- `--max-edge-temp <c>`: Edge temperature threshold for adaptive throttling.
- `--max-hotspot-temp <c>`: Hotspot temperature threshold.
- `--max-power <w>`: Power consumption threshold.
- `--no-adaptive`: Disable temperature-based stress test throttling.

## Output

On exit, AMDS generates:
- `amds_diag.log`: Detailed timestamped execution log.
- `exports/amds.json`: Machine-readable telemetry data.
- `exports/telemetry.csv`: Time-series data for graphing.
- `exports/report.md`: Human-readable diagnostic summary.

## Safety

This software can heavily stress GPUs and may freeze unstable hardware or the full system.

**Use it at your own risk.**
