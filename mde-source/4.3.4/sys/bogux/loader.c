/**
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
 *
 * Load an ELF executable into client space.
 * @file
 */

#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <elf.h>
#include <tnslock.h>

#include <hv/hypervisor.h>

#include <arch/chip.h>
#include <arch/cycle.h>
#include <arch/sim.h>
#include <arch/spr.h>
#include <arch/icache.h>

#include "bogux.h"
#include "debug.h"
#include "files.h"
#include "loader.h"
#include "mman.h"
#include "argv.h"
#include "syscall.h"
#include "messaging.h"
#include "mem_layout.h"
#include "pa_allocator.h"


uint64_t ts_user_start_cycle _TILESTATE;

const char* ts_exec_path _TILESTATE;

int ts_rerun _TILESTATE;

/** Jump to user code */
extern void
jump_user(VirtualAddress user_func_va, VirtualAddress user_stack_pointer);



/** Set MPLs in a way that gives user code the desired access.
 * See sys/hv/hw_config.c for how the hypervisor does it.
 * We will likely want to add more here.
 */
static void
set_mpls()
{
  // Enable (set to one) a bunch of MPLs for user level, i.e. SPR_MPL_xxx_SET_0
  __insn_mtspr(SPR_MPL_SWINT_0_SET_0, 1);

  __insn_mtspr(SPR_MPL_UDN_ACCESS_SET_0, 1);
  __insn_mtspr(SPR_MPL_UDN_AVAIL_SET_0, 1);
  __insn_mtspr(SPR_MPL_UDN_COMPLETE_SET_0, 1);
  __insn_mtspr(SPR_MPL_UDN_TIMER_SET_0, 1);

  __insn_mtspr(SPR_MPL_WORLD_ACCESS_SET_0, 1);

  // Set the MPL for interrupt control to 0 (user level)
  __insn_mtspr(SPR_MPL_INTCTRL_0_SET_0, 0);
}

/** On error, free up the memory we allocated. */
static inline void
free_exec_data(ExecData* data)
{
  free_page(data->size, data->base);
}

/* Pick an arbitrary limit for how much we're willing to copy */
#define MAX_ARGS 1024

int
build_exec_data(ExecData* data, bool user, bool flush,
                const char* filename, char **argv, char** envp)
{
  // Validate and get the size of the filename.
  if (user && !is_valid_user_string(filename, PROT_READ))
    return -EFAULT;

  SYSCALL_TRACE("exec/spawn: filename is %s\n", filename);

  int filename_len = strlen(filename)+1;
  filename_len = ROUND_UP(filename_len, sizeof(long));
  if (filename_len > MAX_ARGS)
    return -E2BIG;

  // Validate and get the size of argv.
  ArrayInfo a = strings_size(argv, user);
  if (!a.valid)
    return -EFAULT;
  if (a.size > MAX_ARGS)
    return -E2BIG;

  // Validate and get the size of envp.
  ArrayInfo e = strings_size(envp, user);
  if (!e.valid)
    return -EFAULT;
  if (e.size > MAX_ARGS)
    return -E2BIG;

  // Handy values
  int argc = a.count;
  int envc = e.count;

  // Allocate enough physical memory for this data.
  // This memory will be mapped into the exec'ed program.
  enum { WORD_SIZE_IN_BYTES = sizeof(long) };
  int argc_len = WORD_SIZE_IN_BYTES;
  int argv_ptrlen = WORD_SIZE_IN_BYTES * (argc + 1);
  int envp_ptrlen = WORD_SIZE_IN_BYTES * (envc + 1);
  static int elf_attributes = 6;
  int elfinfo_len = WORD_SIZE_IN_BYTES * (elf_attributes+1) * 2;
  int argv_stringlen = a.size;
  int envp_stringlen = e.size;

  // For now we round such that the stack starts on the
  // highest "color" page, a la pa_allocator.h
  int total_size = argc_len + argv_ptrlen + envp_ptrlen + elfinfo_len +
    argv_stringlen + envp_stringlen + filename_len;
  total_size = ROUND_UP(total_size, HV_PAGE_SIZE_SMALL * PA_COLORS);
  VirtualAddress sp = MEM_USER_TOP - total_size;
  HV_PhysAddr pa = get_new_page(total_size, sp, ts_controller);

  // Copy data into memory via a temporary mapping
  char* va = map_window(pa, total_size);
  if (va == NULL)
  {
    free_exec_data(data);
    return -ENOMEM;
  }

  // Locate everything in the temporary mapping
  long* argc_p = (long*) va;
  char** argv_ptr = (char**) ((char*) argc_p + argc_len);
  char** envp_ptr = (char**) ((char*) argv_ptr + argv_ptrlen);
  long* elfinfo = (long*) ((char*) envp_ptr + envp_ptrlen);
  char* argv_charptr = ((char*) elfinfo) + elfinfo_len;
  char* envp_charptr = argv_charptr + argv_stringlen;
  char* filename_ptr = envp_charptr + envp_stringlen;

  // Set up the return data.
  data->size = total_size;
  data->base = pa;
  data->file = filename_ptr - va;

  // Write the data to the mapped page(s).
  memset(va, 0, total_size);
  strcpy(filename_ptr, filename);
  *argc_p = argc;
  long delta = (char*) sp - va;
  strings_copy(a, argv_ptr, argv_charptr, argv, delta);
  strings_copy(e, envp_ptr, envp_charptr, envp, delta);
  *elfinfo++ = AT_PAGESZ;
  *elfinfo++ = HV_PAGE_SIZE_SMALL;
  *elfinfo++ = AT_RANDOM;
  *elfinfo++ = (long) argv_charptr + delta;   // not very random; oh well.
  *elfinfo++ = AT_SECURE;
  *elfinfo++ = 0;
  *elfinfo++ = AT_ENTRY;
  *elfinfo++ = 0;    // FIXME: what to put here?
  *elfinfo++ = AT_PHNUM;
  data->phnum = (char*) elfinfo - va;
  *elfinfo++ = -1;   // Initialized later.
  *elfinfo++ = AT_PHDR;
  data->phdr = (char*) elfinfo - va;
  *elfinfo++ = -1;   // Initialized later.
  if (flush || ts_default_oloc_enabled)
  {
    for (char* p = va; p < filename_ptr+filename_len; p += CHIP_L2_LINE_SIZE())
      __insn_flush(p);
    __insn_mf();
  }
  unmap_window(va, total_size);

  return 0;  // success
}

static int pidlock _LOCKS;
static volatile uint64_t idle_tiles _LOCKS;
static volatile int num_idle_tiles _LOCKS;
static uint8_t pid_gen[MAX_TILES] _LOCKS;

static bool
add_to_idle_tiles(uint32_t tile_id, int delta)
{
  assert(tile_id < 64);
  tnslock_rawlock(&pidlock);
  bool was_idle = (idle_tiles & (1ULL << tile_id)) != 0;
  if (delta == 1)
  {
    idle_tiles |= (1ULL << tile_id);
    if (!was_idle)
    {
      num_idle_tiles++;
    }
  }
  else if (delta == -1)
  {
    idle_tiles &= ~(1ULL << tile_id);
    if (was_idle)
    {
      num_idle_tiles--;
      pid_gen[tile_id]++;
    }
  }
  tnslock_rawunlock(&pidlock);
  return was_idle;
}

static bool
all_tiles_idle()
{
  tnslock_rawlock(&pidlock);
  int retval = (num_idle_tiles == num_avail_tiles);
  tnslock_rawunlock(&pidlock);
  return retval;
}

bool
unidle_tile(uint32_t tile_id)
{
  return add_to_idle_tiles(tile_id, -1);
}

bool
is_tile_idle(uint32_t tile_id)
{
  return add_to_idle_tiles(tile_id, 0);
}

pid_t
tile_to_pid(uint32_t tile_id)
{
  pid_t retval;
  tnslock_rawlock(&pidlock);
  if (idle_tiles & (1ULL << tile_id))
    retval = -1;
   else
    retval = (1 << 16) + (pid_gen[tile_id] << 8) + tile_id;
  tnslock_rawunlock(&pidlock);
  return retval;
}

bool
pid_active(pid_t pid)
{
  return (pid > 0) && (tile_to_pid(pid & 0x3F) == pid);
}

static bool ts_napping _TILESTATE;

void
nap_user()
{
  // We're not currently running anything
  add_to_idle_tiles(my_tile_id(), 1);

  // FIXME: volatile cast is due to bug 1106
  *(volatile bool *)&ts_napping = true;

  // If we're supposed to run our initial command again, do so
  if (ts_rerun)
  {
    ts_rerun--;
    load_and_run_default_program();
  }

  // If nobody's running anything at this point, we should halt
  if (all_tiles_idle())
    exit(0);

  // FIXME: we may eventually want to run this at user PL, which
  // means we'd need to mmap up a page with user access and copy
  // a couple of bundles to it and then jump there to spin.
  for (;;)
    asm("nap");
}


static void
sim_notify_exec(const char* binary_name)
{
  uint32_t c;
  do {
    c = ((uint8_t)*binary_name++) << _SIM_CONTROL_OPERATOR_BITS;
    __insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_OS_EXEC | c);
  } while (c);
}


int
do_execve(ExecData* data)
{
  char* map = map_window(data->base, data->size);
  if (map == NULL)
  {
    free_exec_data(data);
    return -ENOMEM;
  }
  int fd = do_open(map + data->file, O_RDONLY, 0);
  unmap_window(map, data->size);
  if (fd < 0)
  {
    free_exec_data(data);
    return fd;
  }

  // Try to read the header
  Elf64_Ehdr elf_header;
  Elf64_Phdr program_header;
  int rc = do_pread(fd, &elf_header, sizeof(elf_header), 0);
  if (rc < 0)
  {
    sys_close(fd);
    free_exec_data(data);
    return rc;
  }

  // Do other header checks
  if (rc != sizeof(elf_header) ||
      elf_header.e_ehsize < sizeof(elf_header) ||
      strncmp((const char*) elf_header.e_ident, ELFMAG, SELFMAG) != 0 ||
      (elf_header.e_machine != CHIP_ELF_TYPE() &&
       elf_header.e_machine != CHIP_COMPAT_ELF_TYPE()) ||
      // Bug 13183: remove uses of xdn from hx build.  Enable when
      // fixed.
#if 0 // #ifndef __tilegx_ise0__
      (elf_header.e_flags & (1 << EF_TILEGX_ISE0)) ||
#endif

      (elf_header.e_flags & (1 << EF_TILEGX_ISE1)) ||

      elf_header.e_type != ET_EXEC ||
      elf_header.e_phentsize < sizeof(program_header))
  {
    sys_close(fd);
    free_exec_data(data);
    return -ENOEXEC;
  }

  // Unmap all user mappings.  This includes MAP_COMMON stuff for
  // completeness, though they will be faulted back in if needed.
  munmap_user_pages();

  // Map top pages of user stack to passed buffer.
  VirtualAddress sp = MEM_USERSTACK_TOP - data->size;
  map_initial_stack(sp, data->base, data->size);

  // Loop through the program header table, and copy sections to memory
  ts_exec_path = (const char*) (sp + data->file);
  int hdr_offset = elf_header.e_phoff;
  VirtualAddress phdr_va = -1UL;
  for (int p = 0; p < elf_header.e_phnum; p++)
  {
    int rc =
      do_pread(fd, &program_header, sizeof (program_header), hdr_offset);
    if (rc != sizeof (program_header))
      panic("%s: failed reading phdr %d: pread %zu at %d returned %d",
            ts_exec_path, p, sizeof(program_header), hdr_offset, rc);

    hdr_offset += elf_header.e_phentsize;

    // If it's not a "LOAD" header, we ignore it.
    if (program_header.p_type != PT_LOAD)
      continue;

    // If it's not readable, we don't load it
    if ((program_header.p_flags & PF_R) == 0 || program_header.p_memsz == 0)
      continue;

    // Allocate pages for this section.
    VirtualAddress section_va = program_header.p_vaddr;
    off_t section_off = program_header.p_offset;

    int prot =
      (PROT_READ |
       ((program_header.p_flags & PF_W) ? PROT_WRITE : 0) |
       ((program_header.p_flags & PF_X) ? PROT_EXEC : 0));

    VirtualAddress map_base = section_va & -HV_PAGE_SIZE_SMALL;
    size_t map_size =
      program_header.p_memsz + (section_va & (HV_PAGE_SIZE_SMALL-1));
    map_size = ROUND_UP(map_size, HV_PAGE_SIZE_SMALL);
    off_t file_base = section_off & -HV_PAGE_SIZE_SMALL;
    size_t file_size =
      program_header.p_filesz + (section_off & (HV_PAGE_SIZE_SMALL-1));

    VirtualAddress mmap_result =
      do_mmap(map_base, map_size, prot | PROT_WRITE,
              MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED,
              0, 0,          // no fd or offset
              ts_controller,
              MMAP_UNINIT);  // leave as garbage; we will fill it in now

    if (MMAP_RESULT_IS_ERROR(mmap_result))
      panic("%s: failed mapping phdr %d: mmap(%#lX,%#zx) returned %ld",
            ts_exec_path, p, map_base, map_size, mmap_result);

    // Copy data from the file to memory.  We copy data in full
    // page sizes since that's what mmap normally does.
    do_pread(fd, (void*)map_base, file_size, file_base);

    // Initialize any remaining section memory size with zeros.
    memset((void*) (section_va + program_header.p_filesz), 0,
           map_base + map_size - section_va - program_header.p_filesz);

    // Remember the highest address we loaded.
    set_brk_start(map_base + map_size);

    // Turn off write protection now that we've loaded it
    if ((prot & PROT_WRITE) == 0)
      sys_mprotect(map_base, map_size, prot);

    // Flush what we've loaded if it was executable
    if ((prot & PROT_EXEC) != 0)
      invalidate_icache((void*)map_base, map_size, HV_PAGE_SIZE_SMALL);

    // Remember where the program headers were loaded.
    if (elf_header.e_phoff >= section_off &&
        elf_header.e_phoff < section_off + program_header.p_filesz)
      phdr_va = elf_header.e_phoff - section_off + section_va;
  }

  // Update ElfInfo that depends on the file contents.
  if (phdr_va == -1UL)
    panic("%s: program headers @%#lx aren't mapped by any PT_LOAD region",
          ts_exec_path, (long) (elf_header.e_phoff));
  *(VirtualAddress*)((char*) sp + data->phdr) = phdr_va;
  *(VirtualAddress*)((char*) sp + data->phnum) = elf_header.e_phnum;

  sys_close(fd);

  // If we were napping before, note that we're not now,
  // and decrement the count of exited processes.
  if (ts_napping)
  {
    ts_napping = false;
    add_to_idle_tiles(my_tile_id(), -1);
  }

  // Drop MPLs prior to going to user mode.
  set_mpls();

  // Record the time that we entered user code.
  ts_user_start_cycle = get_cycle_count();

  // Notify the simulator (if there is one) that we did an 'exec'.
  sim_notify_exec(ts_exec_path);

  // Call the user program.
  jump_user(elf_header.e_entry, sp);

  // This should never be reached.
  panic("Oops, jump_user() returned.");
}
