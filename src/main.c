#include "common.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// Used for selftest logging
#define SELFTEST_FMT_HDR "\n%-4s %-5s %-19s %-27s %s\n"
#define PRINT_SELFTEST_HEADER() \
    printf(SELFTEST_FMT_HDR, "LOAD", "RAN", "CPU", "GPU", "Temp max")
#define PRINT_SELFTEST_ROW(name, status_str, cs, vc, gs, vg, ts) \
    printf("%-4s %-5s %-3s (%-12s)  %-11s (%-12s)  %s\n", \
           (name), (status_str), (cs), (vc), (gs), (vg), (ts))

static void line(const char *k, const char *v) { printf("  %-34s %s\n", k, v); }

void log_cpu_info() {
  printf("== CPU\n");
  char buf[256];
  int n = num_cpus();
  for (int i = 0; i < n; i++) {
    char p[128];
    long long mn = 0, mx = 0, cur = 0;
    snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_min_freq", i); read_ll(p, &mn);
    snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i); read_ll(p, &mx);
    snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", i); read_ll(p, &cur);
    snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/cpufreq/related_cpus", i);
    if (read_text(p, buf, sizeof buf) < 0) strcpy(buf, "?");
    printf("  cpu%-2d %4lld-%4lld MHz (now %4lld)  cluster [%s]\n", i, mn / 1000, mx / 1000, cur / 1000, buf);
  }
  line("/proc/stat", path_status("/proc/stat")); // termux might not have access to this
  line("cpu0 cpuidle residency", path_status("/sys/devices/system/cpu/cpu0/cpuidle/state0/time"));
}

void log_cpuset() {
	char buf[256];
  printf("  %-34s uid=%d  cpuset=", "process", (int)getuid());
  if (read_text("/proc/self/cpuset", buf, sizeof buf) > 0) printf("%s", buf);

  if (strcmp(buf, "/top-app") == 0) {
      printf(" (Handling top-app performance tier, i.e. Termux open)\n");
  } 
  else if (strcmp(buf, "/moderate") == 0) {
      printf(" (Handling moderate performance tier, i.e. Termux on the background)\n");
  } 
  else {
      printf(" (Unknown cpuset tier: %s)\n", buf);
  }
}

void log_gpu_node(const char* path) {
  const char *st = path_status(path);
  if (strcmp(st, "missing") == 0) {
    return;
  }

  char buf[256];
  if (strcmp(st, "ok") == 0) {
    char raw_data[256];
    if (read_text(path, raw_data, sizeof(raw_data)) >= 0) {
      snprintf(buf, sizeof buf, "ok: %s", raw_data);
    } else {
      snprintf(buf, sizeof buf, "error reading node");
    }
  } else {
    snprintf(buf, sizeof buf, "%s", st);
  }
  line(path, buf);
}

void log_gpu_info() {
  printf("== GPU\n");
  const char *gp[] = { "/sys/class/kgsl/kgsl-3d0/gpu_model", "/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage",
                       "/sys/class/kgsl/kgsl-3d0/gpubusy", "/sys/class/kgsl/kgsl-3d0/gpuclk",
                       "/sys/kernel/gpu/gpu_model", "/sys/kernel/gpu/gpu_busy", "/sys/kernel/gpu/gpu_clock",
                       "/sys/class/misc/mali0/device/utilization" };
  for (size_t i = 0; i < sizeof gp / sizeof *gp; i++) {
    log_gpu_node(gp[i]);
  }
}

int cmd_info(int argc, char **argv) {
  (void)argc; (void)argv;
  char buf[256];

  // General device information
  printf("== Device Information\n");
  const char *props[] = { "ro.product.model", "ro.product.device", "ro.soc.manufacturer", "ro.soc.model",
                          "ro.hardware", "ro.board.platform", "ro.build.version.release", "ro.build.version.sdk" };
  for (size_t i = 0; i < sizeof props / sizeof *props; i++) {
    getprop(props[i], buf, sizeof buf);
    if (buf[0]) line(props[i], buf);
  }
	
	// Chipset, CPU and GPU
	log_cpuset();
  log_cpu_info();
  log_gpu_info();

  // Thermal
  int tz = 0, tzok = 0;
  for (int i = 0; i < 256; i++) {
    char p[96];
    long long v;
    snprintf(p, sizeof p, "/sys/class/thermal/thermal_zone%d/temp", i);
    int r = read_ll(p, &v);
    if (r == -ENOENT) break;
    tz++;
    tzok += r == 0;
  }
  printf("== Thermal\n");
  snprintf(buf, sizeof buf, "%d/%d zones readable", tzok, tz);
  line("thermal_zone*/temp", buf);

  // Specific hardware
  printf("== Vulkan devices\n");
  if (vk_list(stdout) < 0) printf("  (vulkan not available)\n");

  monitor_t *m = monitor_new();
  printf("== Monitor will use\n");
  line("cpu utilization", monitor_cpu_source(m));
  line("gpu utilization", monitor_gpu_source(m));
  return 0;
}

typedef struct {
  const char *name;
  int ran, exit_code;
  char result[512];
  int nsamp;
  double cpu, gpu, gmhz, temp;
  int cpu_n, gpu_n, gmhz_n;
} phase_t;

static void accumulate(phase_t *p, const sample_t *s) {
  p->nsamp++;
  if (!isnan(s->cpu_total)) { p->cpu += s->cpu_total; p->cpu_n++; }
  if (!isnan(s->gpu_pct)) { p->gpu += s->gpu_pct; p->gpu_n++; }
  if (!isnan(s->gpu_mhz)) { p->gmhz += s->gpu_mhz; p->gmhz_n++; }
  if (!isnan(s->temp_c) && (isnan(p->temp) || s->temp_c > p->temp)) p->temp = s->temp_c;
}
static double avg(double sum, int n) { return n ? sum / n : NAN; }

typedef int (*cmd_fn)(int, char **);

static void run_phase(monitor_t *m, phase_t *p, cmd_fn fn, char **child_argv, double secs, double interval, int verbose) {
  int pfd[2] = { -1, -1 };
  pid_t pid = -1;
  if (fn) {
    // Fork and call the subcommand directly instead of re-exec'ing /proc/self/exe:
    // under Termux, /proc/self/exe is the system linker (termux-exec launches
    // binaries via linker64), so re-exec fails with CANNOT LINK EXECUTABLE.
    if (pipe(pfd)) { perror("pipe"); return; }
    fflush(stdout); fflush(stderr);
    pid = fork();
    if (pid == 0) {
			// Child function that runs the phase load
      dup2(pfd[1], 1);
      close(pfd[0]); close(pfd[1]);
      int argc = 0;
      while (child_argv[argc]) argc++;
      int rc = fn(argc, child_argv);
      fflush(stdout); fflush(stderr);
      _exit(rc);
    }
    close(pfd[1]);
    fcntl(pfd[0], F_SETFL, O_NONBLOCK);
  }
  char out[16384];
  size_t used = 0;
  sample_t s;
  monitor_sample(m, &s);
  double t0 = now_s(), warm = secs > 4 ? 1.5 : 0.5;
  int alive = pid > 0;
  while (alive ? 1 : now_s() - t0 < secs) {
    sleep_s(interval);
    monitor_sample(m, &s);
    if (now_s() - t0 > warm) accumulate(p, &s);
    if (verbose) { fprintf(stderr, "  [%s] ", p->name); sample_print(m, &s, 0, stderr); }
    if (pid > 0) {
      ssize_t r;
      while (used < sizeof out - 1 && (r = read(pfd[0], out + used, sizeof out - 1 - used)) > 0) used += r;
      int st;
      if (waitpid(pid, &st, WNOHANG) == pid) {
        alive = 0;
        p->ran = 1;
        p->exit_code = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
        while (used < sizeof out - 1 && (r = read(pfd[0], out + used, sizeof out - 1 - used)) > 0) used += r;
        break;
      }
      if (now_s() - t0 > secs + 120) { kill(pid, SIGKILL); }   /* hung driver guard */
    }
  }
  if (pfd[0] >= 0) close(pfd[0]);
  out[used] = 0;
  for (char *l = strtok(out, "\n"); l; l = strtok(NULL, "\n")) {
    if (!strncmp(l, "RESULT ", 7)) snprintf(p->result, sizeof p->result, "%s", l + 7);
    else fprintf(stderr, "  [%s] | %s\n", p->name, l);
  }
}

static const char *verdict(double base, double load, double min_delta, int relative) {
  if (isnan(base) || isnan(load)) {
    return "not-readable";
  }

  double d;
  if (!relative) d = load - base;
  else if (base > 0) d = (load - base) / base * 100;
  else d = load > 0 ? 100 : 0;
  return d >= min_delta ? "observed" : "not-observed";
}

static void jd(FILE *f, double v) { if (isnan(v)) fputs("null", f); else fprintf(f, "%.1f", v); }

int cmd_selftest(int argc, char **argv) {
  double secs = opt_d(argc, argv, "--seconds", 10);
  double interval = opt_d(argc, argv, "--interval", 0.5);
  const char *skip = opt_s(argc, argv, "--skip", "");
  const char *jpath = opt_s(argc, argv, "--json", NULL);
  int verbose = opt_flag(argc, argv, "--verbose");

  monitor_t *m = monitor_new();
  fprintf(stderr, "selftest: %.0fs per phase, cpu source=%s, gpu source=%s\n", secs,
          monitor_cpu_source(m), monitor_gpu_source(m));

  char sbuf[16];
  snprintf(sbuf, sizeof sbuf, "%g", secs);
  char *cpu_argv[] = { "cpu", "--seconds", sbuf, NULL };
  char *gpu_argv[] = { "gpu", "--seconds", sbuf, NULL };

  phase_t ph[3] = { { .name = "idle" }, { .name = "cpu" }, { .name = "gpu" } };
  char **args[3] = { NULL, cpu_argv, gpu_argv };
  cmd_fn fns[3] = { NULL, cmd_cpu, cmd_gpu };
  int nph = 0;
  for (int i = 0; i < 3; i++) ph[i].temp = NAN;
  for (int i = 0; i < 3; i++) {
    if (i && strstr(skip, ph[i].name)) continue;
    fprintf(stderr, "-- phase %s\n", ph[i].name);
    run_phase(m, &ph[i], fns[i], args[i], i ? secs : (secs > 5 ? 5 : secs), interval, verbose);
    nph = i + 1;
    sleep_s(i ? 2 : 0); // let clocks settle between phases
  }
  (void)nph;

  phase_t *idle = &ph[0];
  // Average CPU and GPU during idle (used as baseline)
  double bc = avg(idle->cpu, idle->cpu_n), bg = avg(idle->gpu, idle->gpu_n), bgm = avg(idle->gmhz, idle->gmhz_n);
  int failed = 0;
  FILE *jf = jpath ? fopen(jpath, "w") : NULL;
  if (jpath && !jf) fprintf(stderr, "cannot write %s: %s\n", jpath, strerror(errno));
  if (jf) {
    fprintf(jf, "{\"cpu_source\":\"%s\",\"gpu_source\":\"%s\",\"idle\":{\"cpu_pct\":", monitor_cpu_source(m), monitor_gpu_source(m));
    jd(jf, bc); fputs(",\"gpu_pct\":", jf); jd(jf, bg); fputs(",\"gpu_mhz\":", jf); jd(jf, bgm);
    fputs("},\"phases\":{", jf);
  }
  
  PRINT_SELFTEST_HEADER();
  int first = 1;
  for (int i = 1; i < 3; i++) {
    phase_t *p = &ph[i];
    if (!p->nsamp && !p->ran) continue;
    int ok = p->ran && p->exit_code == 0;
    int skipped = p->ran && p->exit_code == 3;
    failed |= !ok && !skipped;
    double c = avg(p->cpu, p->cpu_n), gp = avg(p->gpu, p->gpu_n), gm = avg(p->gmhz, p->gmhz_n);
		// Compares against baseline (delta of 15 to decide whether we observed the load or not)
    // verdict:
    // - observed: The counter rose by delta (e.g. 15 points).
    // - not-observed: The counter rose but not enough.
    // - not-readable: Either the idle or the current were not readable.
    const char *vc = verdict(bc, c, 15, 0);
    const char *vg = verdict(bg, gp, 15, 0);
    if (strcmp(vg, "observed")) { const char *vf = verdict(bgm, gm, 20, 1); if (strcmp(vf, "not-readable")) vg = vf; }
    char cs[32], gs[48];
    snprintf(cs, sizeof cs, isnan(c) ? "n/a" : "%.0f%%", c);
    if (!isnan(gp)) snprintf(gs, sizeof gs, "%.0f%% @%.0fMHz", gp, gm);
    else if (!isnan(gm)) snprintf(gs, sizeof gs, "n/a @%.0fMHz", gm);
    else snprintf(gs, sizeof gs, "n/a");
    char ts[16];
    snprintf(ts, sizeof ts, isnan(p->temp) ? "n/a" : "%.1fC", p->temp);
    PRINT_SELFTEST_ROW(p->name, ok ? "yes" : skipped ? "SKIP" : "FAIL", cs, vc, gs, vg, ts);
    // if (p->result[0]) printf("     result: %s\n", p->result);
    if (jf) {
      fprintf(jf, "%s\"%s\":{\"ran_ok\":%s,\"skipped\":%s,\"exit\":%d,\"cpu_pct\":", first ? "" : ",", p->name,
              ok ? "true" : "false", skipped ? "true" : "false", p->exit_code);
      jd(jf, c); fputs(",\"gpu_pct\":", jf); jd(jf, gp); fputs(",\"gpu_mhz\":", jf); jd(jf, gm);
      fprintf(jf, ",\"cpu_observed\":\"%s\",\"gpu_observed\":\"%s\",\"temp_max_c\":", vc, vg);
      jd(jf, p->temp);
      fprintf(jf, ",\"result\":%s}", p->result[0] ? p->result : "null");
    }
    first = 0;
  }
  printf("\nThe column for the load's own unit is what matters: cpu row/CPU, gpu row/GPU.\n"
         "'not-readable' = the OS hides that counter from this uid; the load's RESULT line still proves it ran.\n");
  if (jf) { fprintf(jf, "},\"ok\":%s}\n", failed ? "false" : "true"); fclose(jf); }
  return failed ? 1 : 0;
}

static void usage(void) {
  fprintf(stderr,
    "socsonde - CPU/GPU telemetry and load generator for Android\n\n"
    "USAGE:\n"
    "  socsonde info: Provides a capability report for what this uid can read and run\n"
    "  socsonde monitor [--interval S] [--duration S] [--json]: TBD\n"
    "  socsonde cpu [--seconds N] [--threads N | --cpus 0-3,9]: TBD\n"
    "  socsonde gpu [--seconds N] [--iters N] [--groups N] [--batch N] [--device idx]: TBD\n"
    "  socsonde selftest [--seconds N] [--intervals S] [--skip cpu,gpu] [--json FILE] [--verbose]: TBD\n\n");
}

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IOLBF, 0);
  if (argc < 2) {
  	usage();
    return 2;
  }

  const char *c = argv[1];
  argc--; argv++;
  if (!strcmp(c, "info")) return cmd_info(argc, argv);
  if (!strcmp(c, "monitor")) return cmd_monitor(argc, argv);
  if (!strcmp(c, "cpu")) return cmd_cpu(argc, argv);
  if (!strcmp(c, "gpu")) return cmd_gpu(argc, argv);
  if (!strcmp(c, "selftest")) return cmd_selftest(argc, argv);
  usage();
  return 2;
}
