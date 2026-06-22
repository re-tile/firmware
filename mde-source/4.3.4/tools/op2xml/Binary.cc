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

// header file
#include "Binary.h"

// custom includes
#include "Map.h"
#include "global_options.h"


// typedefs
typedef Map <string, Binary*> BinaryMap;


// --------------------------------------------------------------------------
// Binary
// --------------------------------------------------------------------------


// --- members ---

/** Possible "type" attribute value */
const string Binary::EXE = "exe";
 
/** Possible "type" attribute value */
const string Binary::DLL = "dll";
 
/** Possible "type" attribute value */
const string Binary::OS = "os";


// --- constructors/destructors ---

/** Constructor */
Binary::Binary (const string local_path, const string remote_path,
                const string& type)
  : m_local_path(local_path), m_remote_path(remote_path), m_type(type),
    m_from_sample_files(), m_to_sample_files()
{
  // Note: The "find_binaries_in_sample_files" pass resets
  // the type to EXE or OS as needed
}



// -------------------------------------------------------------------------
// friend functions
// -------------------------------------------------------------------------

/**
 * Constructs Binary objects for all binaries in the given sample files
 * and adds them "binaries"
 */
void find_binaries_in_sample_files (
  const SampleFileVector&  sample_file_vector,
  BinaryPtrVector&         binaries
) {
  // This maps local paths to the Binary with that local path:
  BinaryMap binary_map;

  // Iterate over the SampleFiles:
  unsigned int count = sample_file_vector.size();
  for (unsigned int i=0; i<count; ++i) {
    const SampleFile* sample_file_ptr = & sample_file_vector[i];

    // Deal with the SampleFile's "from" path:
    const string& from_local_path  = sample_file_ptr->from_local_path();
    const string& from_remote_path = sample_file_ptr->from_remote_path();

    Binary* matched_binary = binary_map.get (from_remote_path, NULL);
    if (matched_binary == NULL) {
      // Not found: create a new Binary for this path:
      matched_binary = new Binary (from_local_path, from_remote_path);
      binaries.add (matched_binary);
      binary_map.add (from_remote_path, matched_binary);
    }
    matched_binary->add_from_sample_file (sample_file_ptr);

    // Deal with the SampleFile's "to" path:
    const string& to_local_path  = sample_file_ptr->to_local_path();
    const string& to_remote_path = sample_file_ptr->to_remote_path();
    matched_binary = binary_map.get (to_remote_path, NULL);
    if (matched_binary == NULL) {
      // Not found: create a new Binary for this path:
      matched_binary = new Binary (to_local_path, to_remote_path);
      binaries.add (matched_binary);
      binary_map.add (to_remote_path, matched_binary);
    }
    matched_binary->add_to_sample_file (sample_file_ptr);
  }

  // Iterate over the SampleFiles again, assigning the "m_type"
  // of each Binary:
  for (unsigned int i=0; i<count; ++i) {
    const SampleFile* sample_file_ptr = & sample_file_vector[i];
    const string& executable_pathname =
      sample_file_ptr->executable_pathname();
    Binary* matched_binary = binary_map.get (executable_pathname, NULL);
    if (matched_binary != NULL) {
      matched_binary->m_type =
        (executable_pathname == "/var/lib/oprofile/vmlinux") ?
        Binary::OS : Binary::EXE;
    }
  }

  // Show list of binary files we found
  if (g_show_binaries) {
    FOR_EACH(const_iterator, it, BinaryPtrVector, binaries) {
      const Binary* binary = *it;
      cout << binary->to_string() << endl;
    }
  }
}
