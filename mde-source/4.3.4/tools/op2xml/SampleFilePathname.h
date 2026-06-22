// Copyright 2014 Tilera Corporation. All Rights Reserved.
//
//   The source code contained or described herein and all documents
//   related to the source code ("Material") are owned by Tilera
//   Corporation or its suppliers or licensors.  Title to the Material
//   remains with Tilera Corporation or its suppliers and licensors. The
//   software is licensed under the Tilera MDE License.
//
//   Unless otherwise agreed by Tilera in writing, you may not remove or
//   alter this notice or any other notice embedded in Materials by Tilera
//   or Tilera's suppliers or licensors in any way.

// ==========================================================================
// SampleFilePathname.h -- OProfile sample file pathname
// ==========================================================================

// multiple-inclusion guard
#ifndef SAMPLE_FILE_PATHNAME_H
#define SAMPLE_FILE_PATHNAME_H

// C/C++ includes

// custom includes
#include "io.h"       // IO streams
#include "Pathname.h" // Unix pathnames

// OProfile includes

// parsed_filename, parse_filenames(sample_file_path)
#include "parse_filename.h" 
// class extra_images
#include "locate_images.h"


// --------------------------------------------------------------------------
// SampleFilePathname
// --------------------------------------------------------------------------

/*
  OProfile raw sample data is generated in a directory tree with pathnames
  that match these patterns:

 .../var/lib/oprofile/samples/current/   -- sample "session" directory ("current" is default session name)

   - Simple "flat" profile file, which contains (address, value) samples
     {root}/path/to/bin/                 -- enclosing binary file for this sample (i.e. what got run)
       {dep}/{root}/path/of/module/      -- specific module or library for this sample
           EVENT_FILE -- program event

       Note: for these sample files
       - the {dep} path is the binary to use in looking up symbols for sample addresses
       - the address is a virtual memory address minus the start offset of the {dep} binary file


   - Callgraph profile file, which contains (from_address<<32 | to_address, value) samples
     {root}/path/to/bin/                 -- enclosing binary file for this sample (i.e. what got run)
       {dep}/{root}/path/of/from/module/ -- specific module or library for this sample (for "from" address)
         {cg}/{root}/path/of/to/module/  -- specific module or library for this sample (for "to" addresses)
           EVENT_FILE -- program caller/callee event (same module or different modules)

       Note: for these sample files
       - the sample address is a 64-bit value containing the "from" and "to" addresses,
           which as above are offsets from the start offset of the binary files
       - the {dep} path is the binary to use in looking up symbols for from_address values
       - the {cg}  path is the binary to use in looking up symbols for to_address values

   - "Anonymous" sample data (i.e. regions not mapped to a specific binary file)
     The {dep} and/or {cg} paths above may also be of the form:
       {anon:type}/PID.0xLOW_ADDR.0xHIGH_ADDR
     Where "type" is one of "anon","vdso","heap", etc.
     For a Java JVM process, the first such {anon:anon} instance for a given PID also has a corresponding
       {anon:type}/PID.jo
     which is an on-the-fly ELF format file providing Java method names/addresses.
     This is shared amongst all {anon:anon} references to the same JVM process.
     (The .jo extension is meant to parallel with .ko files, i.e. Linux kernel module files.)

   - Kernel invocation sample (?)
     {kern}/name                         -- kernel module name (?)
           EVENT_FILE -- kernel event


   - Kernel invocation sample (?)
     {root}/path/to/bin/                 -- enclosing binary file for this sample (i.e. what got run)
       {dep}/{kern}/name                 -- kernel module name (?)
           EVENT_FILE -- program call into kernel event

  Note: for now, we ignore the latter two kernel cases --
  we'll implement support for them when needed.

  In the above, EVENT_FILE is of the form:
   EVENT_NAME.samplerate.unitmask.tgid.tid.cpu
   - EVENT_NAME is the cpu-specific event name (i.e. ONE, ZERO, TLB_MISS, etc.)
   - samplerate and unitmask are the parameters specified for this event at OProfile init time
   - tgid is the "thread group ID", but is typically just the process ID
   - tid is the thread ID, which may be the same as the process ID if there's only one thread


  For a list of Tilera-specific hardware counter EVENT_NAME IDs,
  refer to either of the following files:

    (your_install_directory)/tile/usr/share/oprofile/tile/tile64/events
    (your_source_tree)/tools/oprofile/events/tile/tile64/events

*/


/** OProfile sample file pathname */
class SampleFilePathname : public Pathname
{
  // --- members ---
private:
  /** Internal struct, contains parsed elements of pathname */
  parsed_filename m_parsed_path;

  /** Cached event count value as a long */
  long m_event_count;

  /** Whether specified filename could be successfully parsed */
  bool m_valid;


  // --- constuctors/destructors ---
public:
  /** Constructor */
  SampleFilePathname();

  /** Constructor */
  SampleFilePathname(const char* path);

  /** Constructor */
  SampleFilePathname(const string& path);

  /** Constructor */
  SampleFilePathname(const Pathname& path);


protected:
  /** Init method */
  void init();


  // --- accessors ---
public:
  /** Whether filename was successfully parsed */
  const bool valid() const;

  /** The full path of the sample file, as used to construct this instance */
  const string pathname() const;

  /** For all sample files, the executing binary */
  const string& executable_pathname() const;


  /** Whether this is a normal or call-graph sample file */
  bool is_callgraph_file() const;


  /** For non-call-graph sample files, the target binary
      that sample addresses are defined in */
  const string& module_pathname() const;


  /** For call-graph sample files, the target binary
      that "from" addresses are defined in */
  const string& from_module_pathname() const;

  /** For call-graph sample files, the target binary
      that "to" addresses are defined in */
  const string& to_module_pathname() const;


  /** Event name */
  const string& event_name() const;

  /** CPU ID */
  const long& event_interval() const;
  
  /** Process ID */
  const string& process_id() const;

  /** Thread ID */
  const string& thread_id() const;

  /** CPU ID */
  const string& cpu_id() const;

};


// --- IO stream operators ---

/** operator<< overload */
OUTPUT_STREAM_OPERATOR(out, SampleFilePathname, sample_file_pathname)
{
  out << sample_file_pathname.pathname();
  return out;
}

// multiple-inclusion guard
#endif
