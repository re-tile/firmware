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
// Binary -- Represents a binary: executable, dll, etc.
// ==========================================================================

// multiple-inclusion guard
#ifndef BINARY_H
#define BINARY_H

// custom includes
#include "SampleFile.h"
#include "Vector.h"


// -------------------------------------------------------------------------
// type definitions
// -------------------------------------------------------------------------

class Binary;   // forward def

/** Vector of Binary pointers */
typedef Vector<Binary*> BinaryPtrVector;


// -------------------------------------------------------------------------
// friend functions
// -------------------------------------------------------------------------

/**
 * Constructs Binary objects for all binaries in the given sample files
 * and adds them to "binaries"
 */
void find_binaries_in_sample_files (
  const SampleFileVector&  sample_file_vector,
  BinaryPtrVector&         binaries
);


// --------------------------------------------------------------------------
// Binary
// --------------------------------------------------------------------------

/**
 * Represents a binary: executable, dll, etc.
 * Each Binary knows its local and remote paths,
 * and has two vectors:
 * one vector of the SampleFiles which name the binary as the "to" binary,
 * and a second vector of the SampleFiles which name the binary
 * as the "from" binary.
 */
class Binary {

  // --- friends ---
  friend void find_binaries_in_sample_files (const SampleFileVector&,
                                             BinaryPtrVector&);

  // --- constants ---

public:
  /** Binary type for compiled executable file */
  static const string EXE;
 
  /** Binary type for shared library file */
  static const string DLL;
 
  /** Binary type for Linux OS binary (i.e. vmlinux) */
  static const string OS;
 

  // --- members ---
  /** The local_path of this Binary */
  string m_local_path;

  /** The remote_path of this Binary */
  string m_remote_path;

  /** The type of this Binary: EXE, DLL or OS */
  string m_type;

  /** The SampleFiles whose "from" PCs are in this Binary */
  SampleFilePtrVector m_from_sample_files;

  /** The SampleFiles whose "to" PCs are in this Binary */
  SampleFilePtrVector m_to_sample_files;


  // --- constructors/destructors ---
 public:
  /** Constructor */
  Binary (const string local_path, const string remote_path,
          const string& type = DLL);


  // --- accessors ---
 public:
  /** Returns a string representation of this binary */
  const string to_string() const {
    std::stringstream ss;
    ss << "Binary["
       << "type=" << m_type
       << ", remote=" << m_remote_path
       << ", local=" << m_local_path
       << "]";
    return ss.str();
  }


  // --- member functions ---
 public:
  /** Returns this Binary's path */
  inline const string& local_path() const { return m_local_path; }

  /** Returns this Binary's path */
  inline const string& remote_path() const { return m_remote_path; }

  /** Returns this Binary's path */
  inline const string& type() const { return m_type; }

  /** Returns the list of files whose "from" PCs fall in this Binary */
  inline const SampleFilePtrVector& from_sample_files() const
  { return m_from_sample_files; }

  /** Returns the list of files whose "to" PCs fall in this Binary */
  inline const SampleFilePtrVector& to_sample_files() const
  { return m_to_sample_files; }

 private:
  /** Adds the given SampleFilePtr to this Binary's list of files
      whose "from" PCs fall in this Binary */
  inline void add_from_sample_file (const SampleFile* sample_file_ptr) {
    m_from_sample_files.add (sample_file_ptr);
  };

  /** Adds the given SampleFilePtr to this Binary's list of files
      whose "to" PCs fall in this Binary */
  inline void add_to_sample_file (const SampleFile* sample_file_ptr) {
    m_to_sample_files.add (sample_file_ptr);
  };
};

// multiple-inclusion guard
#endif
