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

// ============================================================================
// Pathname.h -- UNIX/Linux pathname class
// ============================================================================

// multiple-inclusion guard
#ifndef PATHNAME_H
#define PATHNAME_H

// custom includes
#include "io_utils.h"      // IO streams and utilities
#include "string_utils.h"  // C/C++ strings and utilities
#include "collections.h"   // collections, FOR_EACH macro

// C/C++ includes
#include <sys/stat.h> // struct stat, stat(), lstat()


// ----------------------------------------------------------------------------
// Useful collection types
// ----------------------------------------------------------------------------

/** Array of pathnames */
typedef Array<class Pathname> PathnameArray;

/** Map from pathnames to pathnames (i.e. local to remote) */
typedef Map<class Pathname, class Pathname> PathnameMap;


// ----------------------------------------------------------------------------
// Pathname
// ----------------------------------------------------------------------------

/** UNIX/Linux pathname class */
class Pathname
{
  // --- typedefs ---
public:
  /** size_type typedef, borrowed from string class. */
  typedef std::string::size_type size_type;


  // --- static methods ---
public:
  /** Returns a pathname object for the current working directory */
  static Pathname get_cwd();

  /** Returns a pathname object for the current executable's path. */
  static Pathname get_exe_path();

  /** Returns a pathname object for the current executable's directory.
      With a pathname argument, appends that pathname to the result. */
  static Pathname get_exe_dir_path(const Pathname& subdir = "");

  /** Returns a pathname object for the current exe's installation directory.
      With a pathname argument, appends that pathname to the result. */
  static Pathname get_install_dir_path(const Pathname& subdir = "");

  /** Returns a pathname object for the current exe's install/etc directory.
      With a pathname argument, appends that pathname to the result. */
  static Pathname get_install_etc_dir_path(const Pathname& subdir = "");

  /** Returns a pathname object for the current exe's install/lib directory.
      With a pathname argument, appends that pathname to the result. */
  static Pathname get_install_lib_dir_path(const Pathname& subdir = "");

  /** Returns a pathname object for the current exe's install/tile directory.
      With a pathname argument, appends that pathname to the result. */
  static Pathname get_install_tile_dir_path(const Pathname& subdir = "");


  // --- constants ---
public:
  /** Pathname separator */
  static const char* SEPARATOR;

  /** Pathname separator length */
  static const size_type SEPARATOR_LENGTH;

  /** "not found" position constant, borrowed from string class */
  static const size_type npos;


  // --- members ---
protected:
  /** Pathname string */
  std::string m_pathname;


  // --- constructors ---
public:
  /** Constructor */
  Pathname(const std::string& path = SEPARATOR) :
    m_pathname(path)
  {}

  /** Constructor */
  Pathname(const char* path) :
    m_pathname(path)
  {}

  /** Constructor.
   *  Creates new path by appending specified sub-path.
   */
  Pathname(const Pathname& path, const Pathname& path2) :
    m_pathname(path.m_pathname)
  {
    append(path2);
  }

  /** Copy constructor */
  Pathname(const Pathname& path) :
    m_pathname(path.m_pathname)
  {}

  /** Assignment operator */
  Pathname& operator=(const Pathname& path)
  {
    m_pathname = path.m_pathname;
    return *this;
  }

  /** Destructor */
  ~Pathname()
  {}


  // --- to_string methods ---
public:
  /** Returns pathname as string */
  const std::string& to_string() const;

  /** Returns pathname as character string */
  const char* c_str() const;

  /** String conversion. */
  operator const std::string () const
  {
    return to_string();
  }


  // --- operator overloads ---
public:
  /** Operator overload for append() */
  Pathname& operator+=(const std::string& name)
  {
    append(name);
    return *this;
  }

  /** Operator overload for append() */
  Pathname& operator+=(const Pathname& path)
  {
    append(path);
    return *this;
  }

  /** Comparison operator */
  bool operator==(const Pathname& path) const
  {
    return compare(path) == 0;
  }

  /** Comparison operator */
  bool operator!=(const Pathname& path) const
  {
    return compare(path) != 0;
  }

  /** Comparison operator */
  bool operator<(const Pathname& path) const
  {
    return compare(path) < 0;
  }


  // --- accessors ---
public:
  /** Returns whether this path is empty (i.e. ""). */
  bool empty() const;

  /** Returns simple name of this path,
   *  in other words the final directory/file name in the path.
   *  Special case: the root directory path "/" returns "/".
   */
  std::string get_name() const;

  /** Returns array of directory/file names in path */
  void get_names(StringArray& names) const;

  /** Returns parent directory of this path.
   *  Special case: the root directory path "/" returns "/".
   */
  std::string get_parent_directory() const;

  /** Returns parent directory of this path.
   *  Special case: the root directory path "/" returns "/".
   */
  Pathname get_parent_path() const;

  /** Returns array of names of child files/directories */
  bool get_child_names(StringArray &names) const;


  // --- file tests ---
public:
  
  /** Returns true if path is absolute */
  bool is_absolute() const;

  /** Returns true if path is absolute */
  bool is_relative() const;

  /** Returns true if file exists (i.e. is stat()-able) */
  bool exists() const;

  /** Returns true if this path is a file.
   *  If the path points to a symbolic link, follows the link first.
   */
  bool is_file() const;

  /** Returns true if this path is a directory.
   *  If the path points to a symbolic link, follows the link first.
   */
  bool is_directory() const;

  /** Returns true if this path is a symbolic link. */
  bool is_link() const;


  // --- path manipulation methods ---
public:
  /** comparison for paths */
  int compare(const Pathname& path) const;

  /** Appends two pathname strings */
  static std::string concatenate(const std::string& path1,
                                 const std::string& path2);

  /** Adds name to end of current path */
  void append(const std::string& name);

  /** Adds path to end of current path */
  void append(const Pathname& path);

  /** If path is relative, makes it absolute by appending the
      specified base directory path. */
  void make_absolute(const Pathname& base_path);

  /** If path ends in specified sub-path, or any subset of it,
      removes the sub-path directories. */
  void remove_suffix_path(const Pathname& sub_path);

  /** Searches for first appearance of the specified sub-path.
   *  If found, returns index one greater than last character of match.
   *  If not found, returns npos.
   */
  std::string::size_type index_of_end(const std::string& subpath) const;

  /** Searches for first appearance of the specified sub-path.
   *  If found, returns index one greater than last character of match.
   *  If not found, returns npos.
   */
  std::string::size_type index_of_end(const Pathname& subpath) const;

  /** Splits path at end of first occurrence of specified path,
   *  which may be absolute or relative.
   *  Returns subpaths before the split and after.
   *  Returns true if the specified path was found, false otherwise.
   */
  bool split_path_after(const Pathname& subpath,
                        Pathname& before, Pathname& after) const;


  // --- iterator methods ---

public:

  // frefs
  class iterator;
  class const_iterator;

  /** Returns iterator for beginning of directory/file names in path */
  iterator begin();

  /** Returns const iterator for beginning of directory/file names in path */
  const_iterator begin() const;

  /** Returns iterator for end of directory/file names in path */
  iterator end();

  /** Returns const iterator for end of directory/file names in path */
  const_iterator end() const;


  // --- internal methods ---
private:

  /** Gets current file stat info for this path */
  bool file_stat(struct stat& s, bool followlinks = true) const;


public:

  // --------------------------------------------------------------------------
  // Pathname::const_iterator
  // --------------------------------------------------------------------------

  /** Const iterator class that walks over names in path */
  class const_iterator {

    // --- members ---
  private:
    const std::string& m_pathname;
    size_type m_min, m_max;
    size_type m_start, m_end;
    std::string m_current;
    bool m_special;


    // --- constructors/destructors ---
  public:
    const_iterator(const Pathname& path, bool is_begin = true)
      : m_pathname(path.m_pathname)
    {
      init(path, is_begin);
    }

    ~const_iterator() {}

  protected:
    void init(const Pathname& path, bool is_begin = true);


    // --- operator overloads ---
  public:
    const std::string& operator++(); // prefix

    const std::string& operator--(); // prefix

    const std::string& operator*();

    bool operator==(const const_iterator& i) const;

    bool operator!=(const const_iterator& i) const;

    bool operator>=(const const_iterator& i) const;

    bool operator<(const const_iterator& i) const;


    // --- methods ---
  private:
    size_type next_separator(size_type pos, size_type max);

    size_type prev_separator(size_type pos, size_type min);

    void increment();

    void decrement();

    bool equals(const const_iterator& i) const;

    bool greater_than_or_equal(const const_iterator& i) const;

  };

public:

  // --------------------------------------------------------------------------
  // Pathname::iterator
  // --------------------------------------------------------------------------

  /** Iterator class that walks over names in path */
  class iterator : public const_iterator {

    // --- constructors/destructors ---
  public:
    iterator(Pathname& path, bool is_begin = true)
      : const_iterator(path, is_begin)
    {}

  };

};


// --- String concat operators ---

/** Concatenate a string and a Pathname */
std::string operator+(const std::string& str, const Pathname& path);

/** Concatenate a Pathname and a string */
std::string operator+(const Pathname& path, const std::string& str);


// --- IO stream operators ---

OUTPUT_STREAM_OPERATOR(out, const Pathname&, path)
{
  out << path.to_string();
  return out;
}


// ----------------------------------------------------------------------------
// string utilities for pathnames
// ----------------------------------------------------------------------------

/** Tests whether path starts with specified prefix string. */
bool starts_with(const Pathname& path, const std::string& prefix);

/** Tests whether path ends with specified string. */
bool ends_with(const Pathname& path, const std::string& suffix);


// multiple-inclusion guard
#endif
