/*
 * Test the CPU load with dummy FMA
 */
#include "common.h"
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  int cpu;
  int pinned_ok;
  int ran_on;
  volatile int *stop;
  double mops;
  double sink;
} worker_t;

static void *spin(void *arg) {
  worker_t *w = arg;
  if (w->cpu >= 0) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(w->cpu, &set);
    w->pinned_ok = sched_setaffinity(0, sizeof set, &set) == 0;
  }
  double a = 1.0, b = 2.0, c = 3.0, d = 4.0;
  unsigned long long n = 0;
  double t0 = now_s();
  while (!__atomic_load_n(w->stop, __ATOMIC_RELAXED)) {
    for (int i = 0; i < 1 << 20; i++) {
      a = a * 0.9999999 + 1e-7;
      b = b * 0.9999999 + 1e-7;
      c = c * 0.9999999 + 1e-7;
      d = d * 0.9999999 + 1e-7;
    }
    n++;
  }
  w->mops = n * (double)(1 << 20) * 4 / (now_s() - t0) / 1e6;
  w->sink = a + b + c + d;
	w->ran_on = sched_getcpu();
  return NULL;
}

int cmd_cpu(int argc, char **argv) {
  double secs = opt_d(argc, argv, "--seconds", 10);
  const char *cpus = opt_s(argc, argv, "--cpus", NULL);

  cpu_set_t allowed;
  CPU_ZERO(&allowed);
  sched_getaffinity(0, sizeof allowed, &allowed);
  char cpuset[128] = "?";
  read_text("/proc/self/cpuset", cpuset, sizeof cpuset);
	char allowed_s[256] = "";
  for (int i = 0, len = 0; i < CPU_SETSIZE && i < MAXCPU * 4; i++) {
    if (CPU_ISSET(i, &allowed)) {
      len += snprintf(allowed_s + len, sizeof allowed_s - len, "%s%d", len ? "," : "", i);
		}
  }

  int ids[MAXCPU], n;
  if (cpus) {
 		n = parse_cpu_list(cpus, ids, MAXCPU);
  } else {
    n = (int)opt_d(argc, argv, "--threads", CPU_COUNT(&allowed));
    for (int i = 0; i < n; i++) ids[i] = -1;
  }
  if (n <= 0) { fprintf(stderr, "no threads to run\n"); return 2; }

  // Reject upfront if the user requests for a CPU that is not available in the current
  // cpu set.
  for (int i = 0; i < n; i++) {
    if (ids[i] >= 0 && (ids[i] >= CPU_SETSIZE || !CPU_ISSET(ids[i], &allowed))) {
      fprintf(stderr, "cpu%d is not available in cpuset %s (allowed: %s)\n", ids[i], cpuset, allowed_s);
      printf("RESULT {\"ok\":false,\"error\":\"cpu%d not allowed\",\"cpuset\":\"%s\",\"allowed_cpus\":\"%s\"}\n",
             ids[i], cpuset, allowed_s);
      return 3;
    }
  }

  printf("cpuset=%s allowed_cpus=", cpuset);
  for (int i = 0, first = 1; i < CPU_SETSIZE && i < MAXCPU * 4; i++)
    if (CPU_ISSET(i, &allowed)) { printf("%s%d", first ? "" : ",", i); first = 0; }
  printf(" threads=%d seconds=%.0f\n", n, secs);
  fflush(stdout);

  // Setting up the workers
  volatile int stop = 0;
  worker_t *w = calloc(n, sizeof *w);
  pthread_t *th = calloc(n, sizeof *th);
  for (int i = 0; i < n; i++) {
    w[i].cpu = ids[i];
    w[i].stop = &stop;
    pthread_create(&th[i], NULL, spin, &w[i]);
  }
  sleep_s(secs);
  __atomic_store_n(&stop, 1, __ATOMIC_RELAXED);
  double total = 0;
  int pin_fail = 0;
  for (int i = 0; i < n; i++) {
    pthread_join(th[i], NULL);
    total += w[i].mops;
    if (w[i].cpu >= 0 && !w[i].pinned_ok) {
 		  pin_fail++;
		}
    if (w[i].cpu >= 0) {
			// If the pinned failed, most likely ran on a background process and
      // we specified a core that is not available.
 	    printf("  thread %d -> cpu%d %s %.0f M fma/s (ran on cpu%d)\n", i, w[i].cpu,
             w[i].pinned_ok ? "" : "(PIN FAILED)", w[i].mops, w[i].ran_on);
    }
  }
  int ok = total > 0 && pin_fail == 0;
  printf("total %.0f M fma/s across %d threads\n", total, n);
  printf("RESULT {\"ok\":%s,\"threads\":%d,\"mfma_per_s\":%.0f,\"pin_failures\":%d,\"cpuset\":\"%s\"}\n",
         ok > 0 ? "true" : "false", n, total, pin_fail, cpuset);

  free(w);
  free(th);
  return ok ? 0 : 1;
}
