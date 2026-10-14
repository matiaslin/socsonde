#pragma once
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#define MAXCPU 32

// Used in utils
double now_s(void);
void sleep_s(double s);
int read_text(const char *path, char *buf, size_t n);
int read_ll(const char *path, long long *out);
const char *path_status(const char *path);
int num_cpus(void);
double opt_d(int argc, char **argv, const char *name, double def);
const char *opt_s(int argc, char **argv, const char *name, const char *def);
int opt_flag(int argc, char **argv, const char *name);
double to_mhz(long long v);
int parse_cpu_list(const char *s, int *out, int max);
void getprop(const char *key, char *out, size_t n);

// Used in monitor
typedef struct {
  double t;
  int ncpu;
  double cpu_total;
  double cpu_pct[MAXCPU];
  double cpu_mhz[MAXCPU];
  double gpu_pct;
  double gpu_mhz;
  double temp_c;
} sample_t;

typedef struct monitor monitor_t;
monitor_t *monitor_new(void);
void monitor_sample(monitor_t *m, sample_t *s);
const char *monitor_cpu_source(monitor_t *m);
const char *monitor_gpu_source(monitor_t *m);
void sample_print(monitor_t *m, const sample_t *s, int json, FILE *f);

// Subcommands
int cmd_info(int argc, char **argv);
int cmd_monitor(int argc, char **argv);
int cmd_cpu(int argc, char **argv);
int cmd_gpu(int argc, char **argv);
int cmd_selftest(int argc, char **argv);

// Used in gpuload
int vk_list(FILE *f);
