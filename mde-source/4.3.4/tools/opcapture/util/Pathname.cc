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
// Pathname.cc -- UNIX/Linux pathname class
// ============================================================================

#include "Pathname.h"

// C/C++ includes
#include <dirent.h>        // DIR, dirent
#include <unistd.h>        // getcwd

// custom includes
#include "string_utils.h"  // strlen()


// ----------------------------------------------------------------------------
// Pathname
// ----------------------------------------------------------------------------

// --- static methods ---

/** Returns a pathname object for the current working directory. */
Pathname
Pathname::get_cwd()
{
  int maxlen = MAX_PATH_LEN;
  char dir[maxlen];
  getcwd(dir, maxlen);
  return Pathname(dir);
}

/** Returns a pathname object for the current executable's path. */
Pathname
Pathname::get_exe_pathname()
{
  int maxlen = MAX_PATH_LEN;
  char path[maxlen];
  // Use /proc/self/exe to get our path -- this is Linux-specific.
  int n = readlink("/proc/self/exe", path, maxlen);
  if (n < 0) n = 0;
  path[n] = '\0';
  return Pathname(path);
}

/** Returns a pathname object for the currect working directory.
    With a subdir pathname argument, appends that pathname to the result. */
Pathname
Pathname::get_cwd_pathname(const Pathname& subdir)
{
  return Pathname(get_cwd(), subdir);
}

/** Returns a pathname object for the currect executable's directory.
    With a subdir pathname argument, appends that pathname to the result. */
Pathname
Pathname::get_exe_dir_pathname(const Pathname& subdir)
{
  Pathname exe_path = get_exe_pathname();
  return Pathname(exe_path.get_parent_pathname(), subdir);
}

/** Returns a pathname object for the current exe's installation directory.
    With a subdir pathname argument, appends that pathname to the result. */
Pathname
Pathname::get_install_dir_pathname(const Pathname& subdir)
{
  Pathname exe_path = get_exe_pathname();
  return Pathname(exe_path.get_parent_pathname().get_parent_pathname(), subdir);
}

/** Returns a pathname object for the current exe's installation/etc directory.
    With a subdir pathname argument, appends that pathname to the result. */
Pathname
Pathname::get_install_etc_dir_pathname(const Pathname& subdir)
{
  return Pathname(Pathname(Pathname::get_install_dir_pathname(), "etc"), subdir);
}

/** Returns a pathname object for the current exe's install/lib directory.
    With a subdir pathname argument, appends that pathname to the result. */
Pathname
Pathname::get_install_lib_dir_pathname(const Pathname& subdir)
{
  return Pathname(Pathname(Pathname::get_install_dir_pathname(), "lib"), subdir);
}

/** Returns a pathname object for the current exe's install/tile directory.
    With a subdir pathname argument, appends that pathname to the result. */
Pathname
Pathname::get_install_tile_dir_pathname(const Pathname& subdir)
{
  return Pathname(Pathname(Pathname::get_install_dir_pathname(), "tile"), subdir);
}


// --- constants ---

/** Pathname separator */
const char* Pathname::SEPARATOR = "/";

/** Pathname separator length */
const Pathname::size_type Pathname::SEPARATOR_LENGTH = strlen(SEPARATOR);

/** "not found" position constant, borrowed from string class */
const Pathname::size_type Pathname::npos = std::string::npos;


// --- to_string methods ---

/** Returns pathname as C++ string */
std::string Pathname::to_string() const
{
  return m_pathname;
}

/** Returns pathname as C string */
const char* Pathname::c_str() const
{
  return to_string().c_str();
}

// --- accessors ---

/** Returns whether this path is empty (i.e. ""). */
bool Pathname::is_empty() const
{
  return m_pathname == "";
}

/** Returns pathname length as string. */
Pathname::size_type Pathname::length() const
{
  return m_pathname.length();
}

/** Returns the final directory/file name in the path.
 *  Special case: the root directory path "/" returns "/".
 */
std::string Pathname::get_name() const
{
  if (m_pathname == "/") return m_pathname;
  std::string::size_type last_separator = m_pathname.rfind(SEPARATOR);
  return (last_separator == std::string::npos) ? m_pathname :
    m_pathname.substr(last_separator+1);
}

/** Returns array of directory/file names in path */
void Pathname::get_names(StringArray& names) const
{
  names.clear();
  if (m_pathname.empty())
  {
    // nothing to do
  }
  else if (m_pathname == SEPARATOR)
  {
    // special case, "/" just returns "/"
    names.add(SEPARATOR);
  }
  else
  {
    // iterate over separator-separated strings
    size_type len = m_pathname.size();
    size_type pos = 0, next_pos;
    if (starts_with(m_pathname, SEPARATOR)) pos = SEPARATOR_LENGTH;

    // while we can still find another separator, grab the substring
    // before it
    while (npos != (next_pos = m_pathname.find(SEPARATOR, pos)))
    {
      names.add(m_pathname.substr(pos, next_pos - pos));
      pos = next_pos + SEPARATOR_LENGTH;
    }

    // if there's anything left over, take it as the last name in the path
    if (pos < len)
    {
      names.add(m_pathname.substr(pos, len - pos));
    }
  }
}

/** Returns parent directory of this path as string.
 *  Special case: the root directory path "/" returns "/".
 */
std::string Pathname::get_parent_directory() const
{
  if (m_pathname == "/") return m_pathname;
  std::string::size_type last_separator = m_pathname.rfind(SEPARATOR);
  if (last_separator == 0)
  {
    // must be something like /foobar, so return "/"
    return "/";
  }
  else if (last_separator == std::string::npos)
  {
    // something like "foobar", return as-is
    return m_pathname;
  }
  else
  {
    // something like "...foo/bar", return "...foo"
    return m_pathname.substr(0, last_separator);
 }
}

/** Returns parent directory of this path as pathname.
 *  Special case: the root directory path "/" returns "/".
 */
Pathname Pathname::get_parent_pathname() const
{
  return Pathname(get_parent_directory());
}

/** Constructs array of names of child files/directories */
bool Pathname::get_child_names(StringArray& names) const
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
      std::string name = dirp->d_name;

      // skip UNIX current/parent directory names
      if (name != "." && name != "..")
        names.add(name);
    }
    closedir(dp);
    result = true;
  }

  return result;
}


/** Constructs array of pathnames of child files/directories */
bool Pathname::get_child_pathnames(PathnameArray& pathnames) const
{
  bool result = true;

  StringArray names;
  result = get_child_names(names);

  if (result)
  {
    FOR_EACH(const_iterator, it, StringArray, names)
    {
      const std::string& name = *it;
      pathnames.add(Pathname(*this, name));
    }
  }

  return result;
}


// --- file tests ---

/** Returns true if path is absolute */
bool Pathname::is_absolute() const
{
  return (! m_pathname.empty() && starts_with(m_pathname, SEPARATOR));
}

/** Returns true if path is absolute */
bool Pathname::is_relative() const
{
  return (! m_pathname.empty() && ! starts_with(m_pathname, SEPARATOR));
}

/** Returns true if file exists (i.e. is stat()-able) */
bool Pathname::exists() const
{
  struct stat stat;
  return file_stat(stat);
}

/** Returns true if file exists and is readable by the current user. */
bool Pathname::is_readable() const
{
  struct stat stat;
  return
    file_stat(stat) &&
    ((stat.st_mode & S_IRUSR) != 0);
}

/** Returns true if file exists and is writable by the current user. */
bool Pathname::is_writable() const
{
  struct stat stat;
  return
    file_stat(stat) &&
    ((stat.st_mode & S_IWUSR) != 0);
}

/** Returns true if file exists and is readable/executable by the current user. */
bool Pathname::is_executable() const
{
  struct stat stat;
  return
    file_stat(stat) &&
    ((stat.st_mode & S_IRUSR) != 0) &&
    ((stat.st_mode & S_IXUSR) != 0);
}

/** Returns true if this path exists and is a file.
 *  If the path points to a symbolic link, follows the link first.
 */
bool Pathname::is_file() const
{
  struct stat stat;
  bool exists = file_stat(stat);
  return exists && S_ISREG(stat.st_mode);
}

/** Returns true if this path exists and is a directory.
 *  If the path points to a symbolic link, follows the link first.
 */
bool Pathname::is_directory() const
{
  struct stat stat;
  bool exists = file_stat(stat);
  return exists && S_ISDIR(stat.st_mode);
}

/** Returns true if this path exists and is a symbolic link. */
bool Pathname::is_link() const
{
  struct stat stat;
  bool exists = file_stat(stat, false); // don't follow links
  return exists && S_ISDIR(stat.st_mode);
}


// --- path manipulation methods ---

/** comparison for paths */
int Pathname::compare(const Pathname& path) const
{
  int result = 0;

  Pathname::const_iterator left      = begin();
  Pathname::const_iterator left_end  = end();

  Pathname::const_iterator right     = path.begin();
  Pathname::const_iterator right_end = path.end();

  // compare names in the two paths, as long as both have one more
  while (result == 0 && left < left_end && right < right_end)
  {
    if (*left > *right)
    {
      // left is greater, stop here
      result = 1;
    }
    else if (*left < *right)
    {
      // left is lesser, stop here
      result = -1;
    }
    else
    {
      // otherwise, names are the same, keep going
      ++left;
      ++right;
    }
  }

  // if paths are of different lengths, shorter is lesser
  if (result == 0)
  {
    if (left == left_end && right != right_end)
    {
      result = -1;
    }
    else if (left != left_end && right == right_end)
    {
      result = 1;
    }
  }

  return result;
}

/** Adds name to end of current path */
void Pathname::append(const std::string& name)
{
  if (name.empty()) return; // Nothing to do.

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
std::string Pathname::concatenate(const std::string& path1,
                                  const std::string& path2)
{
  std::string result;

  if (path1.empty())
  {
    result += path2;
  }
  else if (path2.empty())
  {
    result += path1;
  }
  else if (ends_with(path1, SEPARATOR))
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
  if (is_relative())
  {
    m_pathname = concatenate(base_path.to_string(), m_pathname);
  }
}

/** If path ends in specified sub-path, or any subset of it,
    removes the sub-path directories. */
void Pathname::remove_suffix_pathname(const Pathname& sub_path)
{
  Pathname temp = sub_path;
  while (! temp.is_empty())
  {
    // if current path ends in remaining sub_path, remove it and stop
    if (ends_with(m_pathname, temp.to_string()))
    {
      m_pathname =
        m_pathname.substr(0, m_pathname.size() - temp.m_pathname.size());
      break;
    }
    // otherwise, if we've whittled sub_path down to root, quit
    if (temp == "/") break;
    // else knock one directory off sub_path and try again
    temp = temp.get_parent_directory();
  }
}

/** Splits path at end of first occurrence of specified path,
 *  which may be absolute or relative.
 *  Returns subpaths before the split and after.
 *  Returns true if the specified path was found, false otherwise.
 *  If split was not found, both before is set to entire path, after to empty string.
 */
bool Pathname::split_path_after(const Pathname& subpath, 
                                Pathname& before, Pathname& after) const
{
  bool result = false;
  size_type split = npos;

  // find point to split, if any
  if (subpath.is_absolute())
  {
    split = m_pathname.find(subpath.to_string());
    if (split != npos) split += subpath.length();
  }
  else // subpath is relative
  {
    // first see if topmost directory name matches exactly
    split = m_pathname.find("/" + subpath.to_string());
    if (split != npos) split += subpath.length() + 1;
    else
    {
      // FIXME: we take a chance on matching part of a directory name here
      split = m_pathname.find(subpath.to_string());
      if (split != npos) split += subpath.length();
    }
  }

  // now split the path
  split_path(split, before, after);

  return result;
}

/** Splits path at beginning of first occurrence of specified path,
 *  which may be absolute or relative.
 *  Returns subpaths before the split and after.
 *  Returns true if the specified path was found, false otherwise.
 */
bool Pathname::split_path_before(const Pathname& subpath, 
                                Pathname& before, Pathname& after) const
{
  bool result = false;
  size_type split = npos;

  // find point to split, if any
  if (subpath.is_absolute())
  {
    split = m_pathname.find(subpath.to_string());
  }
  else // subpath is relative
  {
    // first see if topmost directory name matches exactly
    split = m_pathname.find("/" + subpath.to_string());
    if (split != npos) split += 1;
    else
    {
      // FIXME: we take a chance on matching part of a directory name here
      split = m_pathname.find(subpath.to_string());
    }
  }

  // now split the path
  split_path(split, before, after);

  return result;
}


/** Splits path at specified character index.
 *  Returns subpaths before the split and after.
 */
void Pathname::split_path(const Pathname::size_type split,
                          Pathname& before, Pathname& after) const
{
  if (split == npos)
  {
    before = m_pathname;
    after = "";
  }
  else
  {
    size_type len = m_pathname.size();

    // split at index
    before = m_pathname.substr(0, split);
    after  = m_pathname.substr(split, len-split);

    // if before string ends with path separator, drop it
    if (! before.m_pathname.empty() && ends_with(before, SEPARATOR))
    {
      before.m_pathname.erase(before.m_pathname.size() - strlen(SEPARATOR));
    }

    // if after string starts with path separator, drop it
    if (! after.m_pathname.empty() && starts_with(after, SEPARATOR))
    {
      after.m_pathname.erase(0, strlen(SEPARATOR));
    }
  }
}



// --- iterator methods ---

// --- constructors/destructors ---
void Pathname::const_iterator::init(const Pathname& path, bool is_begin)
{
  size_type len = m_pathname.size();
  m_special = false;
  if (len == 0 || m_pathname == SEPARATOR)
  {
    // special cases: "" has no names, "/" has the single name "/"
    m_special = true;
    m_min = 0;
    m_max = len;
    if (is_begin)
    {
      // begin iterator
      m_start = m_min;
      m_end   = m_max;
    }
    else
    {
      // end iterator
      m_start = m_max;
      m_end   = m_max;
    }
  }
  else
  {
    m_min = 0;
    m_max = len;
    // ignore beginning/ending separators to make iteration simpler
    if (starts_with(m_pathname, SEPARATOR)) m_min = SEPARATOR_LENGTH;
    if (ends_with(m_pathname, SEPARATOR))   m_max = len - SEPARATOR_LENGTH;
    if (is_begin)
    {
      // begin iterator
      m_start = m_min;
      m_end   = next_separator(m_start, m_max);
    }
    else
    {
      // end iterator
      m_start = m_max;
      m_end   = m_max;
    }
  }
}


// --- operator overloads ---

const std::string& Pathname::const_iterator::operator++() // prefix
{
  increment();
  return operator*();
}

const std::string& Pathname::const_iterator::operator--() // prefix
{
  decrement();
  return operator*();
}

const std::string& Pathname::const_iterator::operator*()
{
  m_current = m_pathname.substr(m_start, m_end - m_start);
  return m_current;
}

bool Pathname::const_iterator::operator==(const const_iterator& i) const
{
  return equals(i);
}

bool Pathname::const_iterator::operator!=(const const_iterator& i) const
{
  return !equals(i);
}

bool Pathname::const_iterator::operator>=(const const_iterator& i) const
{
  return greater_than_or_equal(i);
}

bool Pathname::const_iterator::operator<(const const_iterator& i) const
{
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

void Pathname::const_iterator::increment()
{
  if (m_special)
  {
    // for special cases, we toggle start between min and max
    m_start = m_max;
  }
  else
  {
    // if start and end are already max, nothing to do
    if (m_start < m_max)
    {

      // if end is at end of path, move start there too
      if (m_end == m_max)
      {
        m_start = m_max;
      }

      // if start/end are both min, move end to next separator
      else if (m_end == m_min)
      {
        m_end = next_separator(m_start, m_max);
      }

      // otherwise, move start to one past end,
      // and move end to next separator
      else
      {
        m_start = m_end + SEPARATOR_LENGTH;
        m_end   = next_separator(m_start, m_max);
      }
    }
  }
}

void Pathname::const_iterator::decrement()
{
  if (m_special)
  {
    // for special cases, we toggle m_start between m_min and m_max
    m_start = m_min;
  }
  else
  {
    // if m_start and m_end are already m_min, nothing to do
    if (m_end > m_min)
    {

      // if m_start is at start of path, move m_end there too
      if (m_start == m_min)
      {
        m_end = m_min;
      }

      // if m_start/m_end are both m_max, move m_start back
      // to just after prev separator
      else if (m_start == m_max)
      {
        m_start = prev_separator(m_end, m_min) + SEPARATOR_LENGTH;
      }

      // otherwise, move end to separator one before start,
      // and move start to just after prev separator
      else
      {
        m_end   = m_start - SEPARATOR_LENGTH;

        // note: when we get to start of string, there's no
        //initial separator at start point
        m_start = prev_separator(m_end - 1, m_min);
        if (m_start > m_min) m_start += SEPARATOR_LENGTH;
      }
    }
  }
}

bool Pathname::const_iterator::equals(const const_iterator& i) const
{
  return (m_start == i.m_start && m_end == i.m_end);
}

bool Pathname::const_iterator::greater_than_or_equal(
  const const_iterator& i) const
{
  return (m_start >= i.m_start && m_end >= i.m_end);
}


Pathname::iterator Pathname::begin()
{
  return Pathname::iterator(*this, true);
}

Pathname::const_iterator Pathname::begin() const
{
  return Pathname::const_iterator(*this, true);
}

Pathname::iterator Pathname::end()
{
  return Pathname::iterator(*this, false);
}

Pathname::const_iterator Pathname::end() const
{
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
std::string operator+(const std::string& str, const Pathname& path)
{
  return str + path.to_string();
}

/** Concatenate a Pathname and a string */
std::string operator+(const Pathname& path, const std::string& str)
{
  return path.to_string() + str;
}


// ----------------------------------------------------------------------------
// std::string utilities
// ----------------------------------------------------------------------------

/** Tests whether path starts with specified prefix string. */
bool starts_with(const Pathname& path, const std::string& prefix)
{
  // explicitly call global function to avoid auto-cast back to Pathname method
  return ::starts_with(path.to_string(), prefix);
}

/** Tests whether path ends with specified string. */
bool ends_with(const Pathname& path, const std::string& suffix)
{
  // explicitly call global function to avoid auto-cast back to Pathname method
  return ::ends_with(path.to_string(), suffix);
}
