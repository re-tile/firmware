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
// Pathname.cc -- UNIX/Linux pathname class
// ==========================================================================

#include "Pathname.h"

// C/C++ includes
#include <dirent.h>   // DIR, dirent
#include <unistd.h>   // getcwd
#include <sys/stat.h> // struct stat, stat(), lstat()
#include <string.h>

// custom includes
#include "foreach.h"  // FOR_EACH() macro


// --------------------------------------------------------------------------
// Pathname
// --------------------------------------------------------------------------

// --- static methods ---

/** Returns a pathname object for the current working directory. */
Pathname
Pathname::get_cwd()
{
  int maxlen = 512;
  char dir[maxlen];
  getcwd(dir, maxlen);
  return Pathname(dir);
}

/** Returns a pathname object for the current executable's path. */
Pathname
Pathname::get_exe_path()
{
  int maxlen = 512;
  char path[maxlen];
  // Use /proc/self/exe to get our path -- this is Linux-specific.
  int n = readlink("/proc/self/exe", path, maxlen);
  if (n < 0) n = 0;
  path[n] = '\0';
  return Pathname(path);
}

/** Returns a pathname object for the currect executable's directory.
    With a subdir argument, appends that subdirectory to the result. */
Pathname
Pathname::get_exe_dir_path(const Pathname& subdir)
{
  Pathname exe_path = get_exe_path();
  return Pathname(exe_path.parent_path(), subdir);
}

/** Returns a pathname object for the current exe's installation directory.
    With a pathname argument, appends that pathname to the result. */
Pathname
Pathname::get_install_dir_path(const Pathname& subdir)
{
  Pathname exe_path = get_exe_path();
  return Pathname(exe_path.parent_path().parent_path(), subdir);
}

/** Returns a pathname object for the current exe's installation/etc directory.
    With a pathname argument, appends that pathname to the result. */
Pathname
Pathname::get_install_etc_dir_path(const Pathname& subdir)
{
  return Pathname(Pathname(Pathname::get_install_dir_path(), "etc"), subdir);
}

/** Returns a pathname object for the current exe's install/lib directory.
    With a pathname argument, appends that pathname to the result. */
Pathname
Pathname::get_install_lib_dir_path(const Pathname& subdir)
{
  return Pathname(Pathname(Pathname::get_install_dir_path(), "lib"), subdir);
}

/** Returns a pathname object for the current exe's install/tile directory.
    With a pathname argument, appends that pathname to the result. */
Pathname
Pathname::get_install_tile_dir_path(const Pathname& subdir)
{
  return Pathname(Pathname(Pathname::get_install_dir_path(), "tile"), subdir);
}

// --- constants ---

/** Pathname separator */
const char* Pathname::SEPARATOR = "/";

/** Pathname separator length */
const Pathname::size_type Pathname::SEPARATOR_LENGTH = strlen(SEPARATOR);

/** "not found" position constant, borrowed from string class */
const Pathname::size_type Pathname::npos = string::npos;


// --- to_string methods ---

/** Returns pathname as string */
const string& Pathname::to_string() const
{
  return m_pathname;
}

/** Returns pathname as character string */
const char* Pathname::c_str() const
{
  return to_string().c_str();
}

// --- accessors ---

/** Returns whether this path is empty (i.e. ""). */
bool Pathname::empty() const
{
  return m_pathname == "";
}

/** Returns the final directory/file name in the path.
 *  Special case: the root directory path "/" returns "/".
 */
string Pathname::name() const
{
  if (m_pathname == "/") return m_pathname;
  string::size_type last_separator = m_pathname.rfind(SEPARATOR);
  return (last_separator == string::npos) ? m_pathname :
    m_pathname.substr(last_separator+1);
}

/** Returns array of directory/file names in path */
void Pathname::names(StringVector& names) const
{
  names.clear();
  if (m_pathname.empty()) {
    // nothing to do
  }
  else if (m_pathname == SEPARATOR) {
    // special case, "/" just returns "/"
    names.add(SEPARATOR);
  }
  else {
    // iterate over separator-separated strings
    size_type len = m_pathname.size();
    size_type pos = 0, next_pos;
    if (starts_with(m_pathname, SEPARATOR)) pos = SEPARATOR_LENGTH;

    // while we can still find another separator, grab the substring
    // before it
    while (npos != (next_pos = m_pathname.find(SEPARATOR, pos))) {
      names.add(m_pathname.substr(pos, next_pos - pos));
      pos = next_pos + SEPARATOR_LENGTH;
    }

    // if there's anything left over, take it as the last name in the path
    if (pos < len) {
      names.add(m_pathname.substr(pos, len - pos));
    }
  }
}

/** Returns parent directory of this path.
 *  Special case: the root directory path "/" returns "/".
 */
string Pathname::parent_directory() const
{
  if (m_pathname == "/") return m_pathname;
  string::size_type last_separator = m_pathname.rfind(SEPARATOR);
  if (last_separator == 0) {
    // must be something like /foobar, so return "/"
    return "/";
  }
  else if (last_separator == string::npos) {
    // something like "foobar", return as-is
    return m_pathname;
  }
  else {
    // something like "...foo/bar", return "...foo"
    return m_pathname.substr(0, last_separator);
 }
}

/** Returns parent directory of this path.
 *  Special case: the root directory path "/" returns "/".
 */
Pathname Pathname::parent_path() const
{
  return Pathname(parent_directory());
}

/** Returns array of names of child files/directories */
bool Pathname::child_names(StringVector &names) const
{
  bool result = false;
  if (! is_directory()) return result;

  DIR* dp;
  struct dirent* dirp;
  names.clear();

  if ((dp = opendir(m_pathname.c_str())) != NULL)
  {
    while ((dirp = readdir(dp)) != NULL)
    {
      string name = dirp->d_name;

      // skip UNIX current/parent directory names
      if (name != "." && name != "..")
        names.add(name);
    }
    closedir(dp);
  }

  return result;
}


// --- file tests ---

/** Returns true if path is absolute */
bool Pathname::is_absolute() const {
  return (! m_pathname.empty() && starts_with(m_pathname, SEPARATOR));
}

/** Returns true if path is absolute */
bool Pathname::is_relative() const {
  return (! m_pathname.empty() && ! starts_with(m_pathname, SEPARATOR));
}

/** Returns true if file exists (i.e. is stat()-able) */
bool Pathname::exists() const
{
  struct stat stat;
  return file_stat(stat);
}

/** Returns true if this path is a file.
 *  If the path points to a symbolic link, follows the link first.
 */
bool Pathname::is_file() const
{
  struct stat stat;
  file_stat(stat);
  return S_ISREG(stat.st_mode);
}

/** Returns true if this path is a directory.
 *  If the path points to a symbolic link, follows the link first.
 */
bool Pathname::is_directory() const
{
  struct stat stat;
  file_stat(stat);
  return S_ISDIR(stat.st_mode);
}

/** Returns true if this path is a symbolic link. */
bool Pathname::is_link() const
{
  struct stat stat;
  file_stat(stat, false); // don't follow links
  return S_ISDIR(stat.st_mode);
}


// --- path manipulation methods ---

/** comparison for paths */
int Pathname::compare(const Pathname& path) const {
  int result = 0;

  Pathname::const_iterator left      = begin();
  Pathname::const_iterator left_end  = end();

  Pathname::const_iterator right     = path.begin();
  Pathname::const_iterator right_end = path.end();

  // compare names in the two paths, as long as both have one more
  while (result == 0 && left < left_end && right < right_end)
  {
    if (*left > *right) {
      // left is greater, stop here
      result = 1;
    }
    else if (*left < *right) {
      // left is lesser, stop here
      result = -1;
    }
    else {
      // otherwise, names are the same, keep going
      ++left;
      ++right;
    }
  }

  // if paths are of different lengths, shorter is lesser
  if (result == 0) {
    if (left == left_end && right != right_end) {
      result = -1;
    }
    else if (left != left_end && right == right_end) {
      result = 1;
    }
  }

  return result;
}

/** Adds name to end of current path */
void Pathname::append(const string& name)
{
  if (ends_with(m_pathname, SEPARATOR))
  {
    if (starts_with(name, SEPARATOR))
    {
      // have to throw away one of the slashes, so drop it from our path
      m_pathname.erase(m_pathname.size()-1, 1);
      m_pathname += name;
    }
    else
    {
      // parent path has a slash
      m_pathname += name;
    }
  }
  else
  {
    if (starts_with(name, SEPARATOR))
    {
      // child path has a slash
      m_pathname += name;
    }
    else
    {
      // neither path has a slash, need to add one
      m_pathname += SEPARATOR + name;
    }
  }
}

/** Appends two pathname strings */
string Pathname::concatenate(const string& path1, const string& path2)
{
  string result;

  if (ends_with(path1, SEPARATOR))
  {
    if (starts_with(path2, SEPARATOR))
    {
      // have to throw away one of the slashes, so drop it from our path
      result = path1.substr(0, path1.size()-1);
      result += path2;
    }
    else
    {
      // parent path has a slash
      result = path1 + path2;
    }
  }
  else
  {
    if (starts_with(path2, SEPARATOR))
    {
      // child path has a slash
      result = path1 + path2;
    }
    else
    {
      // neither path has a slash, need to add one
      result = path1 + SEPARATOR + path2;
    }
  }
  return result;
}

/** Adds path to end of current path */
void Pathname::append(const Pathname& path)
{
  m_pathname = concatenate(m_pathname, path.to_string());
}

/** If path is relative, makes it absolute by appending the
    specified base directory path. */
void Pathname::make_absolute(const Pathname& base_path)
{
  if (is_relative()) {
    m_pathname = concatenate(base_path.to_string(), m_pathname);
  }
}

/** If path ends in specified sub-path, or any subset of it,
    removes the sub-path directories. */
void Pathname::remove_suffix_path(const Pathname& sub_path)
{
  Pathname temp = sub_path;
  while (! temp.empty()) {
    // if current path ends in remaining sub_path, remove it and stop
    if (ends_with(m_pathname, temp.to_string())) {
      m_pathname =
        m_pathname.substr(0, m_pathname.size() - temp.m_pathname.size());
      break;
    }
    // otherwise, if we've whittled sub_path down to root, quit
    if (temp == "/") break;
    // else knock one directory off sub_path and try again
    temp = temp.parent_directory();
  }
}

/** Searches for first appearance of the specified sub-path.
 *  If found, returns index one greater than last character of match.
 *  If not found, returns npos.
 */
string::size_type Pathname::index_of_end(const string& subpath) const
{
  string::size_type result = m_pathname.find(subpath);
  if (result != npos) {
    result += subpath.size();
  }
  return result;
}

/** Searches for first appearance of the specified sub-path.
 *  If found, returns index one greater than last character of match.
 *  If not found, returns npos.
 */
string::size_type Pathname::index_of_end(const Pathname& subpath) const
{
  return index_of_end(subpath.to_string());
}

/** Splits path at end of first occurrence of specified path,
 *  which may be absolute or relative.
 *  Returns subpaths before the split and after.
 *  Returns true if the specified path was found, false otherwise.
 *  If split was not found, both before and after are set to path.
 */
bool Pathname::split_path_after(const Pathname& subpath, 
                                Pathname& before, Pathname& after) const
{
  bool result = false;
  size_type split = npos;

  // if subpath is absolute, match to start of path only
  if (subpath.is_absolute()) {
    if (starts_with(m_pathname, subpath.to_string())) {
      split = subpath.m_pathname.size();
    }
  }

  // if subpath is relative, find first match within path
  else { // subpath is relative
    split = index_of_end("/" + subpath.to_string());
  }

  // now split the path, if we found a match
  if (split == npos) {
    before = m_pathname;
    after = m_pathname;
  }
  else {
    result = true;
    size_type len = m_pathname.size();

    // split at index
    before = m_pathname.substr(0, split);
    after  = m_pathname.substr(split, len-split);

    // if before string ends with path separator, drop it
    if (! before.m_pathname.empty() && ends_with(before, SEPARATOR)) {
      before.m_pathname.erase(before.m_pathname.size() - strlen(SEPARATOR));
    }

    // if after string starts with path separator, drop it
    if (! after.m_pathname.empty() && starts_with(after, SEPARATOR)) {
      after.m_pathname.erase(0, strlen(SEPARATOR));
    }
  }

  return result;
}


// --- iterator methods ---

// --- constructors/destructors ---
void Pathname::const_iterator::init(const Pathname& path, bool is_begin)
{
  size_type len = m_pathname.size();
  m_special = false;
  if (len == 0 || m_pathname == SEPARATOR) {
    // special cases: "" has no names, "/" has the single name "/"
    m_special = true;
    m_min = 0;
    m_max = len;
    if (is_begin) {
      // begin iterator
      m_start = m_min;
      m_end   = m_max;
    }
    else {
      // end iterator
      m_start = m_max;
      m_end   = m_max;
    }
  }
  else {
    m_min = 0;
    m_max = len;
    // ignore beginning/ending separators to make iteration simpler
    if (starts_with(m_pathname, SEPARATOR)) m_min = SEPARATOR_LENGTH;
    if (ends_with(m_pathname, SEPARATOR))   m_max = len - SEPARATOR_LENGTH;
    if (is_begin) {
      // begin iterator
      m_start = m_min;
      m_end   = next_separator(m_start, m_max);
    }
    else {
      // end iterator
      m_start = m_max;
      m_end   = m_max;
    }
  }
}


// --- operator overloads ---

const string& Pathname::const_iterator::operator++() // prefix
{
  increment();
  return operator*();
}

const string& Pathname::const_iterator::operator--() // prefix
{
  decrement();
  return operator*();
}

const string& Pathname::const_iterator::operator*()
{
  m_current = m_pathname.substr(m_start, m_end - m_start);
  return m_current;
}

bool Pathname::const_iterator::operator==(const const_iterator& i) const {
  return equals(i);
}

bool Pathname::const_iterator::operator!=(const const_iterator& i) const {
  return !equals(i);
}

bool Pathname::const_iterator::operator>=(const const_iterator& i) const {
  return greater_than_or_equal(i);
}

bool Pathname::const_iterator::operator<(const const_iterator& i) const {
  return ! greater_than_or_equal(i);
}


// --- methods ---

Pathname::size_type Pathname::const_iterator::next_separator(
  size_type pos, size_type max)
{
  Pathname::size_type result = m_pathname.find(SEPARATOR, pos);
  if (result == npos || result > max) result = max;
  return result;
}

Pathname::size_type Pathname::const_iterator::prev_separator(
  size_type pos, size_type min)
{
  Pathname::size_type result = m_pathname.rfind(SEPARATOR, pos);
  if (result == npos || result < min) result = min;
  return result;
}

void Pathname::const_iterator::increment() {
  if (m_special) {
    // for special cases, we toggle start between min and max
    m_start = m_max;
  }
  else {
    // if start and end are already max, nothing to do
    if (m_start < m_max) {

      // if end is at end of path, move start there too
      if (m_end == m_max) {
        m_start = m_max;
      }

      // if start/end are both min, move end to next separator
      else if (m_end == m_min) {
        m_end = next_separator(m_start, m_max);
      }

      // otherwise, move start to one past end,
      // and move end to next separator
      else {
        m_start = m_end + SEPARATOR_LENGTH;
        m_end   = next_separator(m_start, m_max);
      }
    }
  }
}

void Pathname::const_iterator::decrement() {
  if (m_special) {
    // for special cases, we toggle m_start between m_min and m_max
    m_start = m_min;
  }
  else {
    // if m_start and m_end are already m_min, nothing to do
    if (m_end > m_min)
    {

      // if m_start is at start of path, move m_end there too
      if (m_start == m_min) {
        m_end = m_min;
      }

      // if m_start/m_end are both m_max, move m_start back
      // to just after prev separator
      else if (m_start == m_max) {
        m_start = prev_separator(m_end, m_min) + SEPARATOR_LENGTH;
      }

      // otherwise, move end to separator one before start,
      // and move start to just after prev separator
      else {
        m_end   = m_start - SEPARATOR_LENGTH;

        // note: when we get to start of string, there's no
        //initial separator at start point
        m_start = prev_separator(m_end - 1, m_min);
        if (m_start > m_min) m_start += SEPARATOR_LENGTH;
      }
    }
  }
}

bool Pathname::const_iterator::equals(const const_iterator& i) const {
  return (m_start == i.m_start && m_end == i.m_end);
}

bool Pathname::const_iterator::greater_than_or_equal(
  const const_iterator& i) const
{
  return (m_start >= i.m_start && m_end >= i.m_end);
}


Pathname::iterator Pathname::begin() {
  return Pathname::iterator(*this, true);
}

Pathname::const_iterator Pathname::begin() const {
  return Pathname::const_iterator(*this, true);
}

Pathname::iterator Pathname::end() {
  return Pathname::iterator(*this, false);
}

Pathname::const_iterator Pathname::end() const {
  return Pathname::const_iterator(*this, false);
}


// --- internal methods ---

/** Gets current file stat info for this path */
bool Pathname::file_stat(struct stat& s, bool followlinks) const
{
  return (followlinks) ?
    (stat(m_pathname.c_str(), &s) == 0) :
    (lstat(m_pathname.c_str(), &s) == 0) ;
}


// --- String concat operators ---

/** Concatenate a string and a Pathname */
string operator+(const string& str, const Pathname& path) {
  return str + path.to_string();
}

/** Concatenate a Pathname and a string */
string operator+(const Pathname& path, const string& str) {
  return path.to_string() + str;
}


// -------------------------------------------------------------------------
// std::string utilities
// -------------------------------------------------------------------------

/** Tests whether path starts with specified prefix string. */
bool starts_with(const Pathname& path, const string& prefix)
{
  return starts_with(path.to_string(), prefix);
}

/** Tests whether path ends with specified string. */
bool ends_with(const Pathname& path, const string& suffix)
{
  return ends_with(path.to_string(), suffix);
}


// -------------------------------------------------------------------------
// file system tree walking utilities
// -------------------------------------------------------------------------

/** Walks all paths in the specified file system tree.
 *  Invokes the callback function for each one.
 */
void walk_pathname_tree(const Pathname& path,
                        void(*callback)(const Pathname& path))
{
  callback(path);
  StringVector childNames;
  path.child_names(childNames);
  FOR_EACH(const_iterator, i, StringVector, childNames) {
    Pathname childPathname(path, *i);
    walk_pathname_tree(childPathname, callback);
  }
}

/** Walks all files in the specified file system tree
 *  Invokes the callback function for each one.
 */
void walk_pathname_tree_files(const Pathname& path,
                              void(*callback)(const Pathname& path))
{
  if (path.is_file()) {
    callback(path);
  }
  else if (path.is_directory()) {
    StringVector childNames;
    path.child_names(childNames);
    FOR_EACH(const_iterator, i, StringVector, childNames) {
      const Pathname childPathname(path, *i);
      walk_pathname_tree_files(childPathname, callback);
    }
  }
}

/** Walks all directories in the specified file system tree
 *  Invokes the callback function for each one.
 */
void walk_pathname_tree_directories(const Pathname& path,
                                    void(*callback)(const Pathname& path))
{
  if (path.is_directory()) {
    callback(path);
    StringVector childNames;
    path.child_names(childNames);
    FOR_EACH(const_iterator, i, StringVector, childNames) {
      Pathname childPathname(path, *i);
      walk_pathname_tree_directories(childPathname, callback);
    }
  }
}

