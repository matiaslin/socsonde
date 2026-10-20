# socsonde

Monitor CPU and GPU for Android phones from inside Termux.

## Install

Download the `.deb` for your phone from the [releases page](https://github.com/matiaslin/socsonde/releases) (`aarch64` for almost all phones), then in Termux:

```sh
curl -LO https://github.com/matiaslin/socsonde/releases/latest/download/socsonde_aarch64.deb
apt install ./socsonde_aarch64.deb
socsonde info
```

### Build from source

```sh
pkg install clang make vulkan-headers
git clone https://github.com/matiaslin/socsonde && cd socsonde
make
./socsonde info
```

`./packaging/build-deb.sh` builds the same `.deb` locally.

## Commands

| Command | Description |
|---|---|
| `info` | What this phone exposes: device, soc, CPU clusters and clocks, vulkan devices |
| `monitor [--interval S] [--duration S] [--json]` | Live CPU % (total and per core), core clocks, GPU % and clock, `--json` prints one JSON object per sample |
| `cpu [--seconds N] [--threads N \| --cpus 0-3,9]` | CPU test load: one multiple-add thread per allowed core, or pinned to specific cores |
| `gpu [--seconds N] [--iters N] [--groups N] [--batch N] [--device IDX]` | GPU test load: a Vulkan compute shader whose result is checked afterwards |
| `selftest [--seconds N] [--ineterval S] [--skip cpu,gpu] [--json FILE] [--verbose]` | Measures idle, then runs each test load while monitoring, and reports whether the counters saw it |

## Sample outputs: `selftest`
```
$ socsonde selftest
LOAD RAN   CPU                 GPU                         Temp max
cpu  yes   100% (observed    )  0% @0MHz    (not-observed)  n/a
gpu  yes   10% (not-observed)  89% @928MHz (observed    )  n/a
```

## Sample outputs: `monitor`
```
$ socsonde monitor
cpu source: cpuidle-estimate | gpu source: /sys/kernel/gpu/gpu_busy | Ctrl-C to stop
CPU  23.6% [ 14  26   9  95  93   0   0   0   0   0]  GPU 100% @ 980MHz
CPU   6.0% [ 20  16  24   0   0   0   0   0   0   0]  GPU 100% @ 980MHz
CPU  16.1% [ 11   9  11   0  21  22  24  21  21  21]  GPU  96% @ 980MHz
CPU   8.5% [ 14  10  10  38  13   0   0   0   0   0]  GPU  87% @ 980MHz
CPU   3.1% [ 10   9  11   0   0   0   0   0   0   0]  GPU  89% @ 980MHz
CPU  11.0% [ 12   8   4  44   0   0   0  42   0   0]  GPU  95% @ 980MHz
CPU   7.7% [ 12   8   7   0  50   0   0   0   0   0]  GPU  97% @ 980MHz
CPU   9.1% [ 22  16  12   0   0   0   0  42   0   0]  GPU  95% @ 980MHz
```

## What to expect on an unrooted phone

**CPU.** Android blocks `/proc/stat` for apps (since Android 8), so in Termux `socsonde` falls back to an estimate from per-core `cpuidle` residency plus clocks (`cpu source: cpuidle-estimate`).

**GPU.** Snapdragon builds expose Adreno counters under `/sys/class/kgsl/kgsl-3d0/`. Exynos builds expose Xclipse under `/sys/kernel/gpu/`. Whether your `uid` may read them is a per-firmware SELinux decision. The output of `info` will make it clear. If utilization is denied but the clock is readable, selftest uses the clock rise as the signal.
