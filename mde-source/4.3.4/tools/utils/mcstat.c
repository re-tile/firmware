/*
 * Copyright 2014 Tilera Corporation. All Rights Reserved.
 *
 *   The source code contained or described herein and all documents
 *   related to the source code ("Material") are owned by Tilera
 *   Corporation or its suppliers or licensors.  Title to the Material
 *   remains with Tilera Corporation or its suppliers and licensors. The
 *   software is licensed under the Tilera MDE License.
 *
 *   Unless otherwise agreed by Tilera in writing, you may not remove or
 *   alter this notice or any other notice embedded in Materials by Tilera
 *   or Tilera's suppliers or licensors in any way.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_MCS 16          // Too big, but big deal.  Note that mc_bitmap_t,
                            // defined below, must be at least this many bits.
typedef uint16_t mc_bitmap_t;

#define MEMPROF_FILE        "/proc/tile/memprof"
#define CPU_INFO_FILE       "/proc/cpuinfo"
#define MEMORY_INFO_FILE    "/proc/tile/memory"

// Support for two different output styles depending on -i vs others

static const char long_header[] =
  "    memory                               % read   % write           \n"
  "controller      % peak      % read         miss      miss    latency\n"
  "--------------------------------------------------------------------\n";

static const char long_mc_sep[] = "\n";

static const char long_mc_no_format[] =
  "       %3d";

static const char long_format[] =
  "         %3d         %3d          %3d       %3d       %4d";

static const char long_missing_mc_format[] = "";

static const char long_report_end[] = "";


static const char compressed_header[] =
  "     mc 0      |     mc 1      |     mc 2      |      mc 3     |\n"
  " sat read  lat | sat read  lat | sat read  lat | sat read  lat |\n";

static const char compressed_mc_sep[] = " |";

static const char compressed_mc_no_format[] = "";

static const char compressed_format[] =
  " %3d  %3d %4d";

static const char compressed_missing_mc_format[] = "                      |";

static const char compressed_report_end[] = "\n";


static const char wide_header[] =
  "         mc 0          |         mc 1          |         mc 2          |          mc 3         |\n"
  " sat read  rm  wm  lat | sat read  rm  wm  lat | sat read  rm  wm  lat | sat read  rm  wm  lat |\n";

static const char wide_mc_sep[] = " |";

static const char wide_mc_no_format[] = "";

static const char wide_format[] =
  " %3d  %3d %3d %3d %4d";

static const char wide_missing_mc_format[] = "                      |";

static const char wide_report_end[] = "\n";


// Exit with "status" after a usage message
static void
usage_exit(int status)
{
  fprintf(stderr,
          "Usage: mcstat -t interval|-i interval|-w interval|--help\n"
          "OR mcstat -c command [arg..]\n");

  fprintf(stderr,
          "\nThe meaning of the output colums is:\n");
  fprintf(stderr,
          "  %% peak (sat)        percent of theoretical peak throughput\n"
          "  %% read (read)       percent of read operations, as opposed to "
                                  "write operations\n"
          "  %% read miss (rm)    percent of read operations that miss an "
                                  "open bank\n"
          "  %% write miss (wm)   percent of write operations that miss an "
                                  "open bank\n"
          "  %% latency (lat)     average load instruction latency in "
                                  "processor cycles\n");

  exit(status);
}


// Exit with an error message
static void
error_exit(const char* message)
{
  fprintf(stderr, "%s\n", message);
  exit(1);
}


// Search "buf" for the string 'value' followed by ':' and a digit string
// (with possible white space around the ':').  Return the numerical value of
// the digit string as a long long by reference in "result"; also, if keynum
// is not NULL, return the numerical value of the first digit string found
// before 'value' in *keynum (or set it to -1 if there are no digits before
// 'value').  Return non-zero to indicate success.
static int
get_value(const char* value, const char* buf, long long* result, int* keynum)
{
  const char* matched_string = strstr(buf, value);

  if (matched_string == NULL)
    return 0;

  if (keynum)
  {
    *keynum = -1;
    for (const char* p = buf; p < matched_string; ++p)
    {
      if (isdigit(*p))
      {
        *keynum = atoi(p);
        break;
      }
    }
  }

  const char* post = index(matched_string, ':');
  if (post == NULL)
    error_exit("No colon");
  *result = strtoll(post + 1, NULL, 0);
  return 1;
}


// Return the number of instruction cycles per second
static long long
get_cpu_rate(void)
{
  FILE* f = fopen(CPU_INFO_FILE, "r");
  char buf[100];
  long long result = -1;

  if (f == NULL)
    error_exit("Could not open " CPU_INFO_FILE);

  while (fgets(buf, sizeof(buf), f) != NULL)
  {
    if (get_value("cpu MHz", buf, &result, NULL))
      break;
  }

  fclose(f);

  if (result == -1)
    error_exit("Could not determine cpu MHz");

  return result * 1000000;
}


// Populate the vector 'values' from the open file 'f'.  Look for all lines
// with the keyword 'key' and add a value for each of these; the index in
// the vector is the keynum from get_value().  An error is reported if more
// than 'max_count' such values are found.  A bitmap describing the elements
// set in values[] is returned in '*values_found'.
static void
get_values(long long values[],
           mc_bitmap_t* values_found,
           int max_count,
           const char* key,
           FILE* f)
{
  char buf[100];
  *values_found = 0;
  while (fgets(buf, sizeof(buf), f) != NULL)
  {
    long long value;
    int keynum;
    if (get_value(key, buf, &value, &keynum))
    {
      if (keynum >= max_count)
        error_exit("Too many values");
      if (*values_found & (1 << keynum))
        error_exit("Duplicate values");
      values[keynum] = value;
      *values_found |= 1 << keynum;
    }
  }
}


// Populate the vector 'mc_rates' with the bytes/second of each shim,
// returning a bitmap of the mcs found by reference in 'mcs_found'.
static void
get_mc_rates(long long mc_rates[], mc_bitmap_t* mcs_found)
{
  FILE* f = fopen(MEMORY_INFO_FILE, "r");

  if (f == NULL)
    error_exit("Cound not open " MEMORY_INFO_FILE);

  get_values(mc_rates, mcs_found, MAX_MCS, "_speed:", f);
  fclose(f);
}


// Support for parsing the memprof file and knowing what the fields mean
typedef struct
{
  const char* const memprof_name;   // Name of field in memprof file
  const int properties;             // Bits indicate properties.  See below
} named_field_t;


// Property bits
#define NF_READ     1
#define NF_WRITE    2
#define NF_HIT      4
#define NF_MISS     8
#define NF_TOTAL   16

// Field indices
#define LATENCY_COUNT 5
#define LATENCY_CYCLES 6

static const named_field_t named_fields[] =
{
  {"_read_hit_count",          NF_READ|NF_HIT},
  {"_read_miss_count",         NF_READ|NF_MISS},
  {"_write_hit_count",         NF_WRITE|NF_HIT},
  {"_write_miss_count",        NF_WRITE|NF_MISS},
  {"_op_count",                NF_TOTAL},
  {"_read_latency_count",      0},
  {"_read_latency_cycles",     0},
};

#define FIELD_COUNT (sizeof(named_fields) / sizeof(named_field_t))

// Does the field number'field_no' have the given 'properties'?  In other
// words, are all the bits from 'properties' also set in the field's
// properties?
static inline int NF_HasProperties(int field_no, int properties)
{
  return (named_fields[field_no].properties & properties) == properties;
}


// Represents a memprof file.
typedef struct
{
  long long cycles;                 // cycle_counter
  // per controller vector parallel to named_fields:
  long long stats[MAX_MCS][FIELD_COUNT];
  mc_bitmap_t mcs_present;          // bitmap of mcs found
} memprof_t;


// Get current memprof data
static memprof_t*
memprof_create(void)
{
  memprof_t* self = (memprof_t*) malloc(sizeof(memprof_t));
  FILE* f = fopen(MEMPROF_FILE, "r");
  char buf[80];

  if (f == NULL)
    error_exit("Cannot open " MEMPROF_FILE);
  if (fgets(buf, sizeof(buf), f) == NULL)
    error_exit("Garbled file: " MEMPROF_FILE);

  // HACK: Assume MEMPROF_FILE starts with "cycles".
  self->cycles = strtoll(buf + sizeof("cycles"), NULL, 10);

  self->mcs_present = 0;

  while (fgets(buf, sizeof(buf), f) != NULL)
  {
    for (int i = 0; i < FIELD_COUNT; ++i)
    {
      long long value;
      int mc_index;
      if (get_value(named_fields[i].memprof_name, buf, &value, &mc_index) &&
          mc_index >= 0)
      {
        self->stats[mc_index][i] = value;
        self->mcs_present |= (1 << mc_index);
        break;
      }
    }
  }

  fclose(f);
  return self;
}

// Return a/b with rounding in the proper direction.
static long long
rounded_divide(long long a, long long b)
{
  // Special case.  Really bad number theory but works in this case because
  // it happens when we get no operations.  We'd prefer bad number theory
  // to a divide by zero fault.
  if (b == 0)
    return 0;
  else
    return (a + b/2) / b;
}


// Given 'mc_rate', the bytes/second of a mc and 'cpu_rate', the clock speed
// of the cpu, return the minimum number of cpu cycles required for a mc
// operation.
static int
max_op_rate(long long mc_rate, long long cpu_rate)
{
  long long cache_line_rate = mc_rate / 64;
  return rounded_divide(cpu_rate, cache_line_rate);
}


// Return the sum of the elements of 'fields' with all the given 'properties',
// in other words which have all the bits set which are set in the word
// 'properties'.
static long long
sum_fields_with_properties(const long long fields[], int properties)
{
  long long sum = 0;
  int i;

  for (i = 0; i < FIELD_COUNT; ++i)
  {
    if (NF_HasProperties(i, properties))
      sum += fields[i];
  }
  return sum;
}

// Calculate the percent of a of b, rounding properly.
static int percent(long long a, long long b)
{
  // See comment in rounded_divide
  if (b == 0)
    return 0;
  else
    return (((a * 1000) / b) + 5) / 10;
}


// Make a nice printout from 'before' and 'after' memprofs, where 'after' is
// assumed to have been taken later than 'before'.
static void
memprof_print_difference(const memprof_t* before, const memprof_t* after,
                         const long long mc_rates[],
                         const mc_bitmap_t mc_rates_present,
                         const int print_all,
                         const char* mc_no_format,
                         const char* sep,
                         const char* format,
                         const char* missing_mc_format,
                         const char* report_end)
{
  if (before->mcs_present != after->mcs_present)
    error_exit("mc present values don't agree");

  if (mc_rates_present != after->mcs_present)
    error_exit("mc present value and memory speed present value don't agree");

  long long cycles = after->cycles - before->cycles;
  long long cpu_rate = get_cpu_rate();

  mc_bitmap_t mcs_left = after->mcs_present;
  for (int i = 0; i < MAX_MCS && mcs_left; ++i, mcs_left >>= 1)
  {
    if (mcs_left & 1)
    {
      long long total_ops =
          sum_fields_with_properties(after->stats[i], NF_TOTAL)
        - sum_fields_with_properties(before->stats[i], NF_TOTAL);
      long long total_reads =
          sum_fields_with_properties(after->stats[i], NF_READ)
        - sum_fields_with_properties(before->stats[i], NF_READ);
      long long read_misses =
          sum_fields_with_properties(after->stats[i], NF_READ|NF_MISS)
        - sum_fields_with_properties(before->stats[i], NF_READ|NF_MISS);
      long long total_writes =
          sum_fields_with_properties(after->stats[i], NF_WRITE)
        - sum_fields_with_properties(before->stats[i], NF_WRITE);
      long long write_misses =
          sum_fields_with_properties(after->stats[i], NF_WRITE|NF_MISS)
        - sum_fields_with_properties(before->stats[i], NF_WRITE|NF_MISS);
      long long latency_count =
        after->stats[i][LATENCY_COUNT] - before->stats[i][LATENCY_COUNT];
      long long latency_cycles =
        after->stats[i][LATENCY_CYCLES] - before->stats[i][LATENCY_CYCLES];
      printf(mc_no_format, i);
      if (print_all)
      {
        printf(format,
               percent(max_op_rate(mc_rates[i], cpu_rate),
                       rounded_divide(cycles, total_ops)),
               percent(total_reads, total_ops),
               percent(read_misses, total_reads),
               percent(write_misses, total_writes),
               (int)rounded_divide(latency_cycles, latency_count));
      }
      else
      {
        printf(format,
               percent(max_op_rate(mc_rates[i], cpu_rate),
                       rounded_divide(cycles, total_ops)),
               percent(total_reads, total_ops),
               (int)rounded_divide(latency_cycles, latency_count));
      }
      fputs(sep, stdout);
    }
    else
      printf(missing_mc_format);

    fflush(stdout);
  }
  fputs(report_end, stdout);
  fflush(stdout);
}


int
main(int argc, char* argv[])
{
  const char* header = NULL;
  const char* sep = NULL;
  const char* format = NULL;
  const char* mc_format = NULL;
  const char* missing_mc_format = NULL;
  const char* report_end = NULL;

  int run_command = 0;
  int just_once = 0;
  int print_all = 0;

  if (argc < 3)
    usage_exit(1);

  if (!strcmp(argv[1], "--help"))
  {
    usage_exit(0);
  }
  else if (!strcmp(argv[1], "-i"))
  {
    header = compressed_header;
    sep = compressed_mc_sep;
    format = compressed_format;
    mc_format = compressed_mc_no_format;
    missing_mc_format = compressed_missing_mc_format;
    report_end = compressed_report_end;
  }
  else if (!strcmp(argv[1], "-w"))
  {
    print_all = 1;

    header = wide_header;
    sep = wide_mc_sep;
    format = wide_format;
    mc_format = wide_mc_no_format;
    missing_mc_format = wide_missing_mc_format;
    report_end = wide_report_end;
  }
  else
  {
    if (!strcmp(argv[1], "-t"))
      just_once = 1;
    else if (!strcmp(argv[1], "-c"))
      run_command = 1;
    else
      usage_exit(1);

    print_all = 1;

    header = long_header;
    sep = long_mc_sep;
    format = long_format;
    mc_format = long_mc_no_format;
    missing_mc_format = long_missing_mc_format;
    report_end = long_report_end;
  }

  long long mc_rates[MAX_MCS];
  mc_bitmap_t mc_rates_present;

  get_mc_rates(mc_rates, &mc_rates_present);

  if (!run_command)
  {
    if (argc != 3)
      usage_exit(1);

    int seconds = atoi(argv[2]);

    memprof_t* before = memprof_create();

    fputs(header, stdout);
    while (1)
    {
      sleep(seconds);
      memprof_t* after = memprof_create();
      memprof_print_difference(before, after,
                               mc_rates, mc_rates_present, print_all,
                               mc_format, sep, format, missing_mc_format,
                               report_end);
      fflush(stdout);

      if (just_once)
        exit(0);

      free(before);
      before = after;
    }
  }

  // ISSUE: Declines to measure any sub-command which exits with this code.
  const int exec_failed_status = 117;

  memprof_t* before = memprof_create();

  pid_t pid = fork();
  if (pid < 0)
    error_exit("Failure in 'fork'.");

  if (pid == 0)
  {
    execvp(argv[2], argv + 2);
    exit(exec_failed_status);
  }

  int status;
  if (waitpid(pid, &status, 0) <= 0)
    error_exit("Failure in waitpid.");

  if (WIFEXITED(status) && WEXITSTATUS(status) == exec_failed_status)
  {
    fprintf(stderr, "mcstat: cannot run '%s'\n", argv[2]);
    exit(1);
  }

  memprof_t* after = memprof_create();
  fputs(header, stdout);
  memprof_print_difference(before, after,
                           mc_rates, mc_rates_present, print_all,
                           mc_format, sep, format, missing_mc_format,
                           report_end);
  exit(0);
}
