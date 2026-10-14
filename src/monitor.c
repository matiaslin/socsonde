/* Monitor the CPU and GPU
 *
 * CPU:  /proc/stat if readable, else an estimate from per-core cpuidle residency.
 * GPU:  vendor sysfs (Adreno kgsl, Samsung /sys/kernel/gpu, Mali), plus devfreq.
 */
#include "common.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { CPU_NONE, CPU_PROCSTAT, CPU_CPUIDLE };

enum {
 G_NONE, // no readable GPU utilization
 G_PCT,  // the file holds a percentage
 G_RATIO // the file holds busy and total, we compute ratio (100 * busy / total)
};

struct monitor {
  int ncpu, cpu_mode, primed;
  unsigned long long pbusy[MAXCPU + 1], ptot[MAXCPU + 1];
  unsigned long long pidle[MAXCPU], puse[MAXCPU];
  long long minkhz[MAXCPU];
  double pt;
  int gpu_mode;
  char gpu_util[256], gpu_freq[300];
};

static int read_procstat(monitor_t *m, unsigned long long *busy, unsigned long long *tot, int *valid) {
  FILE *f = fopen("/proc/stat", "re"); // unless you are rooted, no permission to read this
  if (!f) return -1;
  char line[512];
  int got = 0;
  while (fgets(line, sizeof line, f)) {
    if (strncmp(line, "cpu", 3)) break;
    int idx;
    char *p = line + 3;
    if (*p == ' ') idx = MAXCPU;
    else { idx = atoi(p); if (idx >= m->ncpu) continue; }
    while (*p && *p != ' ') p++;
    unsigned long long v[10] = {0}, t = 0;
    for (int i = 0; i < 10; i++) { v[i] = strtoull(p, &p, 10); t += i < 8 ? v[i] : 0; }
    tot[idx] = t;
    busy[idx] = t - v[3] - v[4];
    valid[idx] = 1;
    got++;
  }
  fclose(f);
  return got > 0 ? 0 : -1;
}

static int read_idle(int cpu, unsigned long long *idle_us, unsigned long long *usage) {
  unsigned long long ti = 0, us = 0;
  int ok = 0;
  // Idle states are numbered state0, state1, ... (3 on a Galaxy S26 Exynos).
  for (int k = 0; k < 16; k++) {
    char p[128];
    long long v;
    // The total microseconds spent in that state since boot
    snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/cpuidle/state%d/time", cpu, k);
    int r = read_ll(p, &v);
    if (r == -ENOENT) break;
    if (r == 0) { ti += v; ok = 1; }
    // How many times the core has entered that state
    snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/cpuidle/state%d/usage", cpu, k);
    if (read_ll(p, &v) == 0) us += v;
  }
  *idle_us = ti; *usage = us;
  return ok ? 0 : -1;
}

static double cpu_mhz(int cpu) {
  char p[96];
  long long v;
  snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu);
  return read_ll(p, &v) == 0 ? v / 1000.0 : NAN;
}

static const struct { const char *path; int mode; } gpu_util_cands[] = {
  { "/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage", G_PCT },   /* Adreno */
  { "/sys/class/kgsl/kgsl-3d0/gpubusy", G_RATIO },             /* Adreno: "busy total" */
  { "/sys/kernel/gpu/gpu_busy", G_PCT },                       /* Samsung (Mali / Xclipse) */
  { "/sys/class/misc/mali0/device/utilization", G_PCT },
  { "/sys/kernel/ged/hal/gpu_utilization", G_PCT },            /* MediaTek, for completeness */
};
static const char *gpu_freq_cands[] = {
  "/sys/class/kgsl/kgsl-3d0/gpuclk",
  "/sys/class/kgsl/kgsl-3d0/devfreq/cur_freq",
  "/sys/kernel/gpu/gpu_clock",
};

monitor_t *monitor_new(void) {
    monitor_t *m = calloc(1, sizeof *m);

    // CPU
    m->ncpu = num_cpus();
    unsigned long long b[MAXCPU + 1], t[MAXCPU + 1];
    int v[MAXCPU + 1] = {0};
    if (read_procstat(m, b, t, v) == 0) m->cpu_mode = CPU_PROCSTAT;
    else {
        unsigned long long a, c;
        for (int i = 0; i < m->ncpu; i++)
            if (read_idle(i, &a, &c) == 0) { m->cpu_mode = CPU_CPUIDLE; break; }
    }
    for (int i = 0; i < m->ncpu; i++) {
        char p[96];
        snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_min_freq", i);
        if (read_ll(p, &m->minkhz[i])) m->minkhz[i] = -1;
    }

    // GPU
    for (size_t i = 0; i < sizeof gpu_util_cands / sizeof *gpu_util_cands; i++) {
        long long x;
        if (read_ll(gpu_util_cands[i].path, &x) == 0) {
            m->gpu_mode = gpu_util_cands[i].mode;
            snprintf(m->gpu_util, sizeof m->gpu_util, "%s", gpu_util_cands[i].path);
            break;
        }
    }
    for (size_t i = 0; i < sizeof gpu_freq_cands / sizeof *gpu_freq_cands; i++) {
        long long x;
        if (read_ll(gpu_freq_cands[i], &x) == 0) {
            snprintf(m->gpu_freq, sizeof m->gpu_freq, "%s", gpu_freq_cands[i]);
            break;
        }
    }
    return m;
}

const char *monitor_cpu_source(monitor_t *m) {
    return m->cpu_mode == CPU_PROCSTAT ? "/proc/stat" : m->cpu_mode == CPU_CPUIDLE ? "cpuidle-estimate" : "none";
}
const char *monitor_gpu_source(monitor_t *m) { return m->gpu_mode ? m->gpu_util : "none"; }

static double temp_max(void) {
    double best = NAN;
    for (int i = 0; i < 256; i++) {
        char p[96];
        long long v;
        snprintf(p, sizeof p, "/sys/class/thermal/thermal_zone%d/temp", i);
        int r = read_ll(p, &v);
        if (r == -ENOENT) break;
        if (r) continue;
        double c = v > 1000 ? v / 1000.0 : (double)v;
        if (c > 0 && c < 130 && (isnan(best) || c > best)) best = c;
    }
    return best;
}

void monitor_sample(monitor_t *m, sample_t *s) {
  memset(s, 0, sizeof *s);
  double t = now_s();
  s->t = t;
  s->ncpu = m->ncpu;
  s->cpu_total = NAN;
  for (int i = 0; i < m->ncpu; i++) { s->cpu_pct[i] = NAN; s->cpu_mhz[i] = cpu_mhz(i); }

  // Monitoring CPU via procstat or idle estimation
  // Only works for rooted processes (for CI we will not be able to use this)
  if (m->cpu_mode == CPU_PROCSTAT) {
    unsigned long long b[MAXCPU + 1] = {0}, tt[MAXCPU + 1] = {0};
    int v[MAXCPU + 1] = {0};
    if (read_procstat(m, b, tt, v) == 0) {
      for (int i = 0; i <= MAXCPU; i++) {
        if (i < MAXCPU && i >= m->ncpu) continue;
        if (m->primed && v[i] && tt[i] > m->ptot[i] && b[i] >= m->pbusy[i]) {
          double pct = 100.0 * (b[i] - m->pbusy[i]) / (tt[i] - m->ptot[i]);
          if (i == MAXCPU) s->cpu_total = pct; else s->cpu_pct[i] = pct;
        }
        if (v[i]) { m->pbusy[i] = b[i]; m->ptot[i] = tt[i]; }
      }
    }
  } else if (m->cpu_mode == CPU_CPUIDLE) {
    double sum = 0;
    int n = 0;
    for (int i = 0; i < m->ncpu; i++) {
      unsigned long long idle, use;
      if (read_idle(i, &idle, &use)) continue;

      if (m->primed && t > m->pt) {
        double wall_us = (t - m->pt) * 1e6;
        double d = (double)(idle - m->pidle[i]);
        double pct;
        if (use == m->puse[i] && idle == m->pidle[i]) {
          // Special case where no idle data exists. Two cases, either it was always
          // working at 100%, or it has been always in a deep sleep state. We use the
          // clock frequency to determine which of these two cases.
          pct = (m->minkhz[i] > 0 && s->cpu_mhz[i] * 1000 > m->minkhz[i]) ? 100 : 0;
        } else {
          // Standard case: (1.0 - idle / elapsed)
          pct = 100.0 * (1.0 - d / wall_us);
        }
        pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
        s->cpu_pct[i] = pct;
        sum += pct;
        n++;
      }
      m->pidle[i] = idle; m->puse[i] = use;
    }
    if (n) {
      s->cpu_total = sum / n;
    }
  }

  // Monitoring GPU
  s->gpu_pct = NAN;
  if (m->gpu_mode == G_PCT) {
    long long v;
    if (read_ll(m->gpu_util, &v) == 0) s->gpu_pct = (double)v;
  } else if (m->gpu_mode == G_RATIO) {
    char b[128];
    unsigned long long busy, tot;
    if (read_text(m->gpu_util, b, sizeof b) > 0 && sscanf(b, "%llu %llu", &busy, &tot) == 2)
      s->gpu_pct = tot ? 100.0 * busy / tot : 0;
  }
  s->gpu_mhz = NAN;
  if (m->gpu_freq[0]) { long long v; if (read_ll(m->gpu_freq, &v) == 0) s->gpu_mhz = to_mhz(v); }

  s->temp_c = temp_max();
  m->pt = t;
  m->primed = 1;
}

static void jnum(FILE *f, double v) { if (isnan(v)) fputs("null", f); else fprintf(f, "%.1f", v); }

void sample_print(monitor_t *m, const sample_t *s, int json, FILE *f) {
  if (json) {
    fprintf(f, "{\"t\":%.3f,\"cpu_total\":", s->t);
    jnum(f, s->cpu_total);
    fputs(",\"cpu_pct\":[", f);
    for (int i = 0; i < s->ncpu; i++) { if (i) fputc(',', f); jnum(f, s->cpu_pct[i]); }
    fputs("],\"cpu_mhz\":[", f);
    for (int i = 0; i < s->ncpu; i++) { if (i) fputc(',', f); jnum(f, s->cpu_mhz[i]); }
    fputs("],\"gpu_pct\":", f); jnum(f, s->gpu_pct);
    fputs(",\"gpu_mhz\":", f); jnum(f, s->gpu_mhz);
    fputs(",\"temp_max_c\":", f); jnum(f, s->temp_c);
    fputs("}\n", f);
  } else {
    fprintf(f, "CPU ");
    if (isnan(s->cpu_total)) fprintf(f, "  n/a "); else fprintf(f, "%5.1f%%", s->cpu_total);
    fprintf(f, " [");
    for (int i = 0; i < s->ncpu; i++) {
      if (isnan(s->cpu_pct[i])) fprintf(f, "%s  -", i ? " " : "");
      else fprintf(f, "%s%3.0f", i ? " " : "", s->cpu_pct[i]);
    }
    fprintf(f, "]  GPU ");
    if (isnan(s->gpu_pct)) fprintf(f, " n/a"); else fprintf(f, "%3.0f%%", s->gpu_pct);
    if (!isnan(s->gpu_mhz)) fprintf(f, " @%4.0fMHz", s->gpu_mhz);
    if (!isnan(s->temp_c)) fprintf(f, "  Tmax %.1fC", s->temp_c);
    fputc('\n', f);
  }
  fflush(f);
}

int cmd_monitor(int argc, char **argv) {
  double interval = opt_d(argc, argv, "--interval", 1.0);
  double duration = opt_d(argc, argv, "--duration", 0);
  int json = opt_flag(argc, argv, "--json");

  monitor_t *m = monitor_new();
  sample_t s;
  if (!json){
    fprintf(stderr, "cpu source: %s | gpu source: %s | Ctrl-C to stop\n",
            monitor_cpu_source(m), monitor_gpu_source(m));
  }

  monitor_sample(m, &s);
  double t0 = now_s();
  for (;;) {
    sleep_s(interval);
    monitor_sample(m, &s);
    sample_print(m, &s, json, stdout);
    if (duration > 0 && now_s() - t0 >= duration) break;
  }
  return 0;
}
