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

/**
 * Test free RAM by repeatedly writing and reading a changing pattern.
 */

// Test just about all available RAM by writing and reading a pattern
// with all available tiles.
// Even though this test will not cover all installed RAM, it covers almost
// all RAM not currently allocated, and it should uncover any intermittent
// signal integrity issues because the memory accesses will be intense.

// This program optionally accepts the following options on the command line:
//  --passes <n> : number of passes through memory to make.
//                 Each pass through takes 3 to 4 seconds.
//  --dur <s>    : Seconds for the test to run (use instead of --passes)
//                 Each pass through takes 3 to 4 seconds.
//  --size <n>   : bytes of memory to test.  (A trailing 'k', 'm', or 'g'
//                 may be used to specify kilo-, mega-, or gigabytes.)

#include <errno.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/fcntl.h>
#include <sys/mman.h>

#include <arch/chip.h>

#include <tmc/alloc.h>
#include <tmc/cpus.h>
#include <tmc/sync.h>
#include <tmc/task.h>

// Number of passes to run if user does not specify.
#define DEFAULT_PASSES 8

// Amount of free memory to leave free.  This needs to account for the
// memory we'll use to run our processes (stack, etc.), as well as memory
// which might be used by other random system activity while we're running.
// This is a minimum; we reserve more than this on machines with more than
// 4 GB.  Also note that this is more of a memory stress test than a total
// memory coverage test, so it's better to have this be a little high than
// a little low.
#define RESERVE_FREE_MEM (512 * 1024 * 1024)

// Alignment (and min size) of blocks of memory sent to each process.
// Needs to be bigger than the L2 Cache to ensure loads and stores go out to
// memory.
#define MEM_BLOCK_ALIGN (CHIP_L2_CACHE_SIZE() * 2)

// Tag used in ilib messages.
#define MSG_TAG 100

// Size of test operation.
#ifndef OP_SIZE
#ifdef __LP64__
#define OP_SIZE 8
#else
#define OP_SIZE 4
#endif
#endif

#if OP_SIZE == 8
#define OP_TYPE uint64_t
#elif OP_SIZE == 4
#define OP_TYPE uint32_t
#elif OP_SIZE == 2
#define OP_TYPE uint16_t
#elif OP_SIZE == 1
#define OP_TYPE uint8_t
#else
#error Bad OP_SIZE, must be 8, 4, 2, or 1
#endif

/** Maximum number of memory controllers; this is way more than we ever
 *  expect to have, but that shouldn't hurt. */
#define MAX_MCS 8

//
// Command options, first the long versions.
//
static const struct option long_options[] =
{
  { .name = "duration",    .has_arg = 1, .val = 'd' },
  { .name = "home",        .has_arg = 1, .val = 'h' },
  { .name = "l2_cache",    .has_arg = 0, .val = '2' },
  { .name = "passes",      .has_arg = 1, .val = 'p' },
  { .name = "size",        .has_arg = 1, .val = 's' },
  { 0 },
};


static void
usage(char* msg)
{
  if (msg)
    fprintf(stderr, "Error: %s\n", msg);

  fprintf(stderr, "Usage: memtest [options]\n");
  fprintf(stderr, " [-p, --passes <#passes>]\n");
  fprintf(stderr, " [-d, --duration <#seconds>]\n");
  fprintf(stderr, " [-s, --size <n>]\n");
  fprintf(stderr, " [-2, --l2_cache]\n");
  fprintf(stderr, " [-h, --home {default | hash | here | none | cpu#}]\n");

  exit(1);
}

//
// Now the short ones.
//
static const char options[] = "p:s:d:";

/** Retrieve the current per-controller correctable ECC error counts.
 *  We use zero for controllers that don't exist or don't support ECC.
 * @param ce_counts Array to fill with counts.
 * @param num_ce_counts Number of controllers to look for.
 */
static void
get_ce_counts(long *ce_counts, int num_ce_counts)
{
  for (int i = 0; i < num_ce_counts; i++)
  {
    char pathname[80];

    snprintf(pathname, sizeof (pathname),
             "/sys/devices/system/edac/mc/mc%d/ce_count", i);
    FILE* f = fopen(pathname, "r");
    if (f != NULL)
    {
      fscanf(f, "%ld", &ce_counts[i]);
      fclose(f);
    }
  }
}


//
// Undefine this to use less-random-looking data; bad for coverage, good
// for debug of certain issues.
//
#define USE_CRC_DATA

/** Convert a string to an integer.  Supports a trailing "k", "m", or "g",
 * which multiplies the result by 2^10, 2^20, or 2^30, respectively.
 * Dies on failure.
 *
 * @param s String to convert.
 * @return Parsed value.
 */
static long long
strtoll_with_suffix(char* s)
{
  char* endptr;
  long long val = strtoll(s, &endptr, 0);

  if (*endptr == 'k' || *endptr == 'K')
  {
    val *= 1024;
    endptr++;
  }
  else if (*endptr == 'm' || *endptr == 'M')
  {
    val *= 1024 * 1024;
    endptr++;
  }
  else if (*endptr == 'g' || *endptr == 'G')
  {
    val *= 1024 * 1024 * 1024;
    endptr++;
  }

  if (*endptr)
    tmc_task_die("Improperly formatted numerical argument '%s'", s);

  return val;
}

/** Convert a virtual address to a client physical address.
 *
 * @param va Virtual address to convert.
 * @return Client physical address.
 */
static unsigned long long
va2cpa(void* va)
{
  char line[128];
  uintptr_t uiva = (uintptr_t) va;
  
  static FILE *fp;
  if (!fp)
  {
    fp = fopen("/proc/self/pgtable", "r");
    if (!fp)
    {
      return ~0ULL;
    }
  }
  else
    rewind(fp);

  uintptr_t pgmsk = ~((uintptr_t) getpagesize() - 1); 

  while (fgets(line, sizeof(line), fp))
  {
    long lineva;
    char prot[16];
    unsigned long long linepa;

    if (sscanf(line, "%lx %s PA=%llx ", &lineva, prot, &linepa) != 3)
      break;

    if ((uiva & pgmsk) == (lineva & pgmsk))
      return linepa + (uiva & ~pgmsk);
  }

  return ~0ULL;
}


/** Memory testing function run by each tile (or process).
 *
 * @param mem_size Size of block in bytes this process is to test.
 * @param total_passes Number of write/read passes through that block to do.
 * @param total_seconds Number of seconds for test to run.
 * @return Number of errors found.
 */
int
test_mem(int rank, int count, unsigned long long mem_size,
         int total_passes, time_t total_seconds, int l2_cache, int home)
{
  unsigned long long mem_words = mem_size / sizeof (OP_TYPE);
  OP_TYPE* mem_area = NULL;

  // Allocate memory buffer this process will test.  Don't bother if the
  // size is larger than we can possibly get.
  if ((size_t) mem_size == mem_size)
  {
    tmc_alloc_t alloc = TMC_ALLOC_INIT;
    tmc_alloc_set_home(&alloc, home);
    mem_area = tmc_alloc_map(&alloc, mem_size);
  }
  if (mem_area == NULL)
    tmc_task_die(
"Process rank %d can't allocate %#llx bytes: %s.\n"
"You may have specified too much memory with the --size option; may have\n"
"specified an illegal --home option; or may need to make more tiles available\n"
"to memtest (%d %s available currently).  Try reducing the memory size, using\n"
"the --size option, or increasing the number of tiles, using the taskset\n"
"command or tile-monitor's --tiles option.",
rank, mem_size, strerror(errno), count, (count == 1) ? "is" : "are");

  // Create masks to xor with pattern generator number, so that all bits
  // are written with both 0 and 1 and different possible data bit shorts are
  // checked.
  static const unsigned long masks[] = {
#if OP_SIZE > 4
    0x0000000000000000UL, 0xffffffffffffffffUL,
    0x3333333333333333UL, 0xccccccccccccccccUL,
    0x9999999999999999UL, 0x6666666666666666UL,
    0x5555555555555555UL, 0xaaaaaaaaaaaaaaaaUL,
#else
    0x00000000, 0xffffffff,
    0x33333333, 0xcccccccc,
    0x99999999, 0x66666666,
    0x55555555, 0xaaaaaaaa,
#endif
  };

  int error_count = 0;
  int pass_num = 0;
  time_t end_time = 0;
  // These values scale the pass count for progress reporting, so we aren't
  // printing dots non-stop in the L2$ test case.
  int pass_shift = (l2_cache) ? 12 : 0;
  int pass_mask = (1 << pass_shift) - 1;

  if (total_seconds)
    end_time = time(NULL) + total_seconds;

  while(1)
  {
    // Makes a better test if each tile is writing different values.
    // Make sure accum is not 0 starting out or the CRC32 instruction will
    // always return 0.
    unsigned long accum = rank + 1;
    // Write the pattern.
    for (unsigned long long addr = 0; addr < mem_words; addr++)
    {
#ifdef USE_CRC_DATA
      accum = __insn_crc32_32(accum, 0);
#if OP_SIZE > 4
      accum = (accum << 32) | __insn_crc32_32(accum, 0);
#endif
      mem_area[addr] = accum ^ masks[pass_num & 7];
#else
      accum = accum;
#if OP_SIZE == 8
      mem_area[addr] = 0xABCD000000000000UL |
        ((unsigned long long) rank << 36) | addr;
#elif OP_SIZE == 4
      mem_area[addr] = (rank << 28) | (addr & 0xFFFFFFF);
#elif OP_SIZE == 2
      mem_area[addr] = (rank << 12) | (addr & 0xFFF);
#elif OP_SIZE == 1
      mem_area[addr] = (rank << 4) | (addr & 0xF);
#endif
#endif
    }  // end for addr: write

    // Read/validate the values just written.
    accum = rank + 1;
    for (unsigned long long addr = 0; addr < mem_words; addr++)
    {
#ifdef USE_CRC_DATA
      accum = __insn_crc32_32(accum, 0);
#if OP_SIZE > 4
      accum = (accum << 32) | __insn_crc32_32(accum, 0);
#endif
      accum = accum;
      OP_TYPE exp_data = accum ^ masks[pass_num & 7];
#else
#if OP_SIZE == 8
      OP_TYPE  exp_data = 0xABCD000000000000UL |
        ((unsigned long long) rank << 36) | addr;
#elif OP_SIZE == 4
      OP_TYPE exp_data = (rank << 28) | (addr & 0xFFFFFFF);
#elif OP_SIZE == 2
      OP_TYPE exp_data = (rank << 12) | (addr & 0xFFF);
#elif OP_SIZE == 1
      OP_TYPE exp_data = (rank << 4) | (addr & 0xF);
#endif
#endif
      OP_TYPE received_data = mem_area[addr];
      if (received_data != exp_data)
      {
        // TODO: We want to print out a PA here, and the DIMM label.
        // TODO: When there are many errors, more than one process can try
        //       to print at the same time, and their messages get jumbled.
        //       It would be nice to somehow make these prints atomic.
        fprintf(stderr, "\nERROR process rank %d: mismatch at VA %p, "
                "CPA 0x%llx.\n", rank, &mem_area[addr],
                va2cpa(&mem_area[addr]));
        fprintf(stderr,
#if OP_SIZE > 4
        "Expected data 0x%016lx but got 0x%016lx (XOR 0x%016lx)\n",
#else
        "Expected data 0x%08x but got 0x%08x (XOR 0x%08x)\n",
#endif
                exp_data, received_data, received_data ^ exp_data);
        error_count++;
      }
    }  // end for addr: read

    // Have a different tile print each progress message so that one
    // tile does not get way behind all the others.
    if ((pass_num >> pass_shift) % count == rank &&
        (pass_num & pass_mask) == 0)
    {
      printf(".");
      fflush(stdout);
    }

    // Update counters (passes or seconds).  We only check the time every
    // pass_mask passes to keep from spending most of our time in libc or
    // the kernel in the L2$ test case.
    pass_num++;
    if (total_passes && pass_num >= total_passes)
      break;
    if (end_time && (pass_num & pass_mask) == 0 && time(NULL) > end_time)
      break;

  } // end while(1)

  tmc_alloc_unmap(mem_area, mem_size);

  return error_count;
}


// Parallelize the current process via "fork", or die.
//
// This forks off "count - 1" (watched) child processes and returns the
// resulting 'rank' in each process (0 for the parent, 1, 2, 3, etc. for
// the children), after locking each process to the rank'th cpu in the
// current affinity set.
//
static int
parallelize(int count)
{
  cpu_set_t cpus;

  if (tmc_cpus_get_my_affinity(&cpus) != 0)
    tmc_task_die("Failure in 'tmc_cpus_get_my_affinity()'.");

  if (tmc_cpus_count(&cpus) < count)
    tmc_task_die("Insufficient cpus (%d < %d).", tmc_cpus_count(&cpus), count);

  int watch_forked_children = tmc_task_watch_forked_children(1);

  int rank;
  for (rank = 1; rank < count; rank++)
  {
    pid_t child = fork();
    if (child < 0)
      tmc_task_die("Failure in 'fork()'.");
    if (child == 0)
      goto done;
  }
  rank = 0;

  (void)tmc_task_watch_forked_children(watch_forked_children);

 done:

  if (tmc_cpus_set_my_cpu(tmc_cpus_find_nth_cpu(&cpus, rank)) < 0)
    tmc_task_die("Failure in 'tmc_cpus_set_my_cpu()'.");

  return rank;
}


int
main(int argc, char* argv[])
{
  int opt;
  int total_passes = DEFAULT_PASSES;
  int l2_cache = 0;
  time_t total_seconds = 0;
  unsigned long long force_free_mem = 0;
  int home = TMC_ALLOC_HOME_DEFAULT;

  while ((opt = getopt_long(argc, argv, options, long_options, NULL)) > 0)
  {
    //
    // Process individual options and their arguments.
    //
    switch (opt)
    {
    case '2':   // --l2_cache
      l2_cache = 1;
      break;

    case 'd':   // --duration
      total_seconds = (time_t) strtoll(optarg, NULL, 0);
      break;

    case 'h':   // --home
      if (!strcmp(optarg, "default"))
        home = TMC_ALLOC_HOME_DEFAULT;
      else if (!strcmp(optarg, "hash"))
        home = TMC_ALLOC_HOME_HASH;
      else if (!strcmp(optarg, "here"))
        home = TMC_ALLOC_HOME_HERE;
      else if (!strcmp(optarg, "none"))
        home = TMC_ALLOC_HOME_NONE;
      else
      {
        char* endp;
        home = strtol(optarg, &endp, 0);
        if (endp == optarg || *endp)
          usage("--home option takes default, hash, here, none, or "
                "a cpu number");
      }
      break;

    case 'p':   // --passes
      total_passes = strtol(optarg, NULL, 0);
      break;

    case 's':   // --size
      force_free_mem = strtoll_with_suffix(optarg);
      break;

    default:
      usage("unknown option");
      break;
    }
  }

  // Exit if neither passes nor duration are greater than 0.
  // If using duration, this takes precedence over passes
  if (!((total_passes > 0) || (total_seconds > 0))) {
    tmc_task_die("Either --passes or --duration must be greater than 0.");
  }
  else if (total_seconds) {
    total_passes = 0;
  }

  cpu_set_t cpus;
  if (tmc_cpus_get_my_affinity(&cpus) != 0)
    tmc_task_die("Failure in 'tmc_cpus_get_my_affinity()'.");

  int count = tmc_cpus_count(&cpus);

  unsigned long long free_mem = 0;
  unsigned long long sequestered_mem = 0;

  if (force_free_mem)
  {
    free_mem = force_free_mem;
  }
  else
  {
    FILE* minfo;
    minfo = fopen("/proc/meminfo", "r");
    if (minfo == NULL)
      tmc_task_die("Could not open /proc/meminfo.");

    // Read one line of meminfo per loop iteration.
    // End when hit end of file, or when the free memory line is found.
    char next_line[80];
    while (fgets(next_line, 80, minfo) != NULL)
    {
      int found = 0;
      if (sscanf(next_line, "MemFree: %lld kB", &free_mem) == 1)
	found++;
      if (sscanf(next_line, "Sequestered: %lld kB", &sequestered_mem) == 1)
	found++;
      if (found == 2)
        break;
    }
    // If the free memory line was never found, free_mem will still be zero,
    // and we'll fail out in the minimum free memory check below.

    // Convert free memory from kB to bytes.
    free_mem <<= 10;

    // Convert sequestered memory from kB to bytes.
    sequestered_mem <<= 10;

    free_mem += sequestered_mem;

    // We make sure to leave at least RESERVE_FREE_MEM, or one eighth of
    // the available free memory, whichever is larger, unused.
    if (RESERVE_FREE_MEM > free_mem / 8)
      free_mem -= RESERVE_FREE_MEM;
    else
      free_mem -= free_mem / 8;
  }

  // Minimum amount of free memory to run a test, based on block alignment.
  unsigned long long min_free_mem = count * MEM_BLOCK_ALIGN;

  if (free_mem < min_free_mem)
    tmc_task_die("Minimum amount of memory to test with %d tiles is "
                 "%llu bytes.", count, min_free_mem);

  // Calculate amount of memory each tile should test; make the number
  // aligned to keep things neat.  If we're just testing the L2$, we just
  // need an L2-sized block per tile.
  unsigned long long mem_size_to_test;
  if (l2_cache)
  {
    home = TMC_ALLOC_HOME_HERE;
    mem_size_to_test = CHIP_L2_CACHE_SIZE();
  }
  else
    mem_size_to_test = (free_mem / count) & -MEM_BLOCK_ALIGN;

  if (total_seconds) {
    printf("Testing %lld MBytes using %d tiles, %d seconds\n",
           (mem_size_to_test * count) >> 20, count, (int) total_seconds);
  }
  else {
    printf("Testing %lld MBytes using %d tiles, %d passes\n",
           (mem_size_to_test * count) >> 20, count, total_passes);
  }

  // Allocate a "barrier" in memory which will be shared between the
  // parent process and its children.
  tmc_alloc_t alloc = TMC_ALLOC_INIT;
  tmc_alloc_set_shared(&alloc);
  size_t shared_size = sizeof(tmc_sync_barrier_t) + sizeof(int) * count;
  void* shared_mem = tmc_alloc_map(&alloc, shared_size);
  if (shared_mem == NULL)
    tmc_task_die("Failed to allocate shared memory.");
  tmc_sync_barrier_t* barrier = (tmc_sync_barrier_t*)shared_mem;
  tmc_sync_barrier_init(barrier, count);

  int* errors_ptr = (int*)(shared_mem + sizeof(*barrier));

  // Get and save the number of correctable ECC errors seen so far.
  long start_ce_count[MAX_MCS] = { 0 };
  get_ce_counts(start_ce_count, MAX_MCS);

  int rank = parallelize(count);

  // Before we start allocating memory, make ourselves a bit more
  // vulnerable to the Linux out-of-memory killer.
  int fd = open("/proc/self/oom_score_adj", O_WRONLY);
  if (fd >= 0)
  {
    char *msg = "50\n";
    write(fd, msg, sizeof(msg));
    close(fd);
  }

  // Run the test on all tiles.
  int errors = test_mem(rank, count, mem_size_to_test,
                        total_passes, total_seconds, l2_cache, home);

  errors_ptr[rank] = errors;

  // Wait for all processes to finish.
  tmc_sync_barrier_wait(barrier);

  // Process 0 collects all the results and prints final message.
  if (rank == 0)
  {
    for (int i = 1; i < count; i++)
      errors += errors_ptr[i];

    //
    // See if we got any correctable ECC errors.  Note that the kernel
    // polls for ECC errors once a second, so we wait a few seconds first
    // to make sure we've counted all errors that happened during the test.
    //
    sleep(2);

    long end_ce_count[MAX_MCS] = { 0 };
    get_ce_counts(end_ce_count, MAX_MCS);

    for (int i = 0; i < MAX_MCS; i++)
    {
      long nerr = end_ce_count[i] - start_ce_count[i];
      if (nerr)
      {
        fprintf(stderr, "Controller %d got %ld correctable memory errors "
                "during test\n", i, nerr);
      }
      errors += nerr;
    }

    if (errors != 0)
    {
      printf("\nMemory test found %d errors.  See details above.\n", errors);
      return 1;
    }

    printf("\nMemory test passed.\n");
    tmc_alloc_unmap(shared_mem, shared_size);
  }

  return 0;
}
