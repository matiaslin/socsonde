#include "common.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

double now_s(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

void sleep_s(double s) {
  if (s <= 0) return;
  struct timespec ts = { (time_t)s, (long)((s - (time_t)s) * 1e9) };
  while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
}

int read_text(const char *path, char *buf, size_t n) {
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return -errno;
  
  ssize_t r = read(fd, buf, n - 1);
  int e = errno;
  close(fd);
  if (r < 0) return -e;

  buf[r] = 0;
  while (r > 0 && (buf[r - 1] == '\n' || buf[r - 1] == ' ')) buf[--r] = 0;
  return (int)r;
}

int read_ll(const char *path, long long *out) {
  char b[128];
  int r = read_text(path, b, sizeof b);
  if (r < 0) return r;
  char *p = b;
  while (*p && !((*p >= '0' && *p <= '9') || *p == '-')) p++;
  if (!*p) return -EINVAL;
  *out = strtoll(p, NULL, 10);
  return 0;
}

const char *path_status(const char *path) {
  char b[64];
  int r = read_text(path, b, sizeof b);
  if (r >= 0) return "ok";
  if (r == -EACCES || r == -EPERM) return "denied";
  if (r == -ENOENT) return "missing";
  return strerror(-r);
}

int parse_cpu_list(const char *s, int *out, int max) {
  int n = 0;
  while (s && *s && n < max) {
    char *end;
    long a = strtol(s, &end, 10), b = a;
    if (end == s) break;
    if (*end == '-') b = strtol(end + 1, &end, 10);
    for (long i = a; i <= b && n < max; i++) out[n++] = (int)i;
    s = (*end == ',') ? end + 1 : NULL;
  }
  return n;
}

int num_cpus(void) {
  char b[64];
  int ids[MAXCPU];
  if (read_text("/sys/devices/system/cpu/possible", b, sizeof b) > 0) {
    int n = parse_cpu_list(b, ids, MAXCPU);
    if (n > 0) return ids[n - 1] + 1;
  }
  long n = sysconf(_SC_NPROCESSORS_CONF);
  return n > MAXCPU ? MAXCPU : (int)n;
}

static int find_opt(int argc, char **argv, const char *name) {
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], name)) return i;
  }
  return -1;
}

double opt_d(int argc, char **argv, const char *name, double def) {
  int i = find_opt(argc, argv, name);
  return (i > 0 && i + 1 < argc) ? atof(argv[i + 1]) : def;
}

const char *opt_s(int argc, char **argv, const char *name, const char *def) {
  int i = find_opt(argc, argv, name);
  return (i > 0 && i + 1 < argc) ? argv[i + 1] : def;
}

int opt_flag(int argc, char **argv, const char *name) {
  return find_opt(argc, argv, name) > 0;
}

double to_mhz(long long v) {
  if (v <= 0) return 0;
  if (v >= 10000000LL) return v / 1e6; // Hz
  if (v >= 10000LL) return v / 1e3; // kHz
  return (double)v; // MHz
}

void getprop(const char *key, char *out, size_t n) {
  out[0] = 0;
#ifdef __ANDROID__
  // Deprecated only in a header comment; unlike __system_property_read_callback
  // it exists on every API level, and the properties we read are short.
  char v[PROP_VALUE_MAX] = "";
  __system_property_get(key, v);
  snprintf(out, n, "%s", v);
#else
  (void)key; (void)n;
#endif
}
