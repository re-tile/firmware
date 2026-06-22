/**
 * @file parse_filename.cpp
 * Split a sample filename into its constituent parts
 *
 * @remark Copyright 2003 OProfile authors
 * @remark Read the file COPYING
 *
 * @author Philippe Elie
 */

#include <stdexcept>
#include <vector>
#include <string>
#include <iostream>
#include <sys/stat.h>

#include "parse_filename.h"
#include "file_manip.h"
#include "string_manip.h"
#include "locate_images.h"

using namespace std;

namespace {

// PP:3.19 event_name.count.unitmask.tgid.tid.cpu
parsed_filename parse_event_spec(string const & event_spec)
{
	typedef vector<string> parts_type;
	typedef parts_type::size_type size_type;

	size_type const nr_parts = 6;

	parts_type parts = separate_token(event_spec, '.');

	if (parts.size() != nr_parts) {
		throw invalid_argument("parse_event_spec(): bad event specification: " + event_spec);
	}

	for (size_type i = 0; i < nr_parts ; ++i) {
		if (parts[i].empty()) {
			throw invalid_argument("parse_event_spec(): bad event specification: " + event_spec);
		}
	}

	parsed_filename result;

	size_type i = 0;
	result.event = parts[i++];
	result.count = parts[i++];
	result.unitmask = parts[i++];
	result.tgid = parts[i++];
	result.tid = parts[i++];
	result.cpu = parts[i++];

	// FIXME: This init code really belongs in a constructor,
	// but I don't want to change the header file too drastically.
	result.jit_dumpfile_exists = false;

	return result;
}


/**
 * @param component  path component
 *
 * remove from path_component all directory left to {root}, {kern}
 */
void remove_base_dir(vector<string> & path)
{
	vector<string>::iterator it;
	for (it = path.begin(); it != path.end(); ++it) {
		if (*it == "{root}" || *it == "{kern}")
			break;
	}

	path.erase(path.begin(), it);
}


/// Handle an anon region. Pretty print the details.
/// The second argument is the anon portion of the path which will
/// contain extra details such as the anon region name (unknown, vdso, heap etc.)
string const parse_anon(string const & anon_id, string const& anon_marker)
{
	string name = anon_marker;
	// Get rid of "{anon:
	name.erase(0, 6);
	// Get rid of the trailing '}'
	name.erase(name.size() - 1, 1);

	vector<string> parts = separate_token(anon_id, '.');
	if (parts.size() != 3)
		throw invalid_argument("parse_anon(), not a value pid.addr.addr identifier: " + anon_id);

	string result = name +" (tgid:";
	result += parts[0] + " range:" + parts[1] + "-" + parts[2] + ")";
	return result;
}

// Returns true if string starts with specified prefix string.
bool starts_with(const string& s, const string& prefix)
{
	return (s.find(prefix,0) == 0);
}


}  // anonymous namespace


/*
 * Valid sample file pathnames are variations on: (for example, not an exhaustive list)
 *
 * .../{kern}/name/event_spec
 * .../{root}/path/to/bin/{dep}/{root}/path/to/bin/event_spec
 * .../{root}/path/to/bin/{dep}/{anon:anon}/pid.start.end/event_spec
 * .../{root}/path/to/bin/{dep}/{anon:[vdso]}/pid.start.end/event_spec
 * .../{root}/path/to/bin/{dep}/{kern}/name/event_spec
 * .../{root}/path/to/bin/{dep}/{root}/path/to/bin/{cg}/{root}/path/to/bin/event_spec
 * .../{root}/path/to/bin/{dep}/{anon...}/pid.start.end/{cg}/{anon...}/pid.start.end/event_spec
 *
 */
parsed_filename parse_filename(string const & filename,
			       extra_images const & extra_found_images)
{
	struct stat st;

	// Split pathname into event_spec and everything preceding it.
	string::size_type pos = filename.find_last_of('/');
	if (pos == string::npos) {
		throw invalid_argument("parse_filename() invalid filename: " +
				       filename);
	}
	string event_spec = filename.substr(pos + 1);
	string pathname_spec = filename.substr(0, pos);

	// Parse event_spec for event, count, unitmask, tgid, gid, cpu.
	parsed_filename result = parse_event_spec(event_spec);

	// Save a copy of the entire pathname of the sample file.
	result.filename = filename;

	// Convert pathname into list of directory names.
	vector<string> path = separate_token(pathname_spec, '/');

	// Drop everything before the first {kern} or {root}.
	// Note that remove_base_dir() leaves an empty list if neither is found.
	remove_base_dir(path);

	// Iterate over directory names to collect binary image path(s).
	size_t i = 0;

	// pp_interface PP:3.19 to PP:3.23
	// A legal path must have at least 2 components (e.g. "{root}/binary_name").
	if (path.size() < 2)
        {
		throw invalid_argument("parse_filename() invalid filename, "
				       "missing initial {root} or {kern} sub-path: " +
				       filename);
	}

	// Skip initial "{root}" or "{kern}" marker.
	++i;

	// Collect image pathname, up to "{dep}" marker.
	for (; i < path.size() ; ++i)
	{
		if (path[i] == "{dep}") break;
		result.image += "/" + path[i];
	}
	if (i == path.size()) 
	{
		throw invalid_argument("parse_filename() invalid filename, "
				       "missing {dep} sub-path: " +
				       filename);
	}

	// Skip "{dep}" marker.
	++i;

	// PP:3.19
	// The {dep} marker must be followed by {kern}, {root}, or {anon...}.
	if (i == path.size() ||
	    (path[i] != "{kern}" && path[i] != "{root}" && ! starts_with(path[i],"{anon")))
	{
		throw invalid_argument("parse_filename() invalid filename, "
				       "{dep} sub-path missing {root}, {kern}, or {anon}: " +
				       filename);
	}

	// Skip {root}, {kern} or {anon...}, but remember whether {dep} sub-path is {anon}.
	bool lib_anon = starts_with(path[i], "{anon");
	++i;

	if (! lib_anon)
	{
		// Collect lib_image pathname, up to end of path or "{cg}" marker, if any.
		for (; i < path.size(); ++i)
		{
			if (path[i] == "{cg}") break;
			result.lib_image += "/" + path[i];
		}
	}

	// For {anon}, process the {anon}/pid.addr.addr identifier.
	else
	{
		// Note: There may be a "{cg}/path" suffix, so ignore it.
		pos = pathname_spec.rfind("{cg}/");

		// Check {anon} identifier for proper format.
		pos = pathname_spec.rfind('.', pos);
		if (pos != string::npos) --pos;
		pos = pathname_spec.rfind('.', pos);
		if (pos == string::npos) {
			throw invalid_argument("parse_filename() invalid filename, "
					       "{dep}/{anon} is not in pid.addr.addr format: " +
					       pathname_spec);
		}

		// If this {anon} came from a Java JVM, and OProfile's JVM agent was used,
		// there'll be a pid.jo pseudo-binary containing Java Class.method() symbols.

		// Construct pathname of this psuedo-binary, and see whether it exists.
		string jitdump = pathname_spec.substr(0, pos) + ".jo";
		if (! stat(jitdump.c_str(), &st))
		{
			// If so, we'll point to this file as if it was the profiled binary image.
			// Later code assumes an optional prefix path
			// is stripped from the lib_image.
			result.lib_image = extra_found_images.strip_path_prefix(jitdump);
			result.jit_dumpfile_exists = true;
		}

		// If not, we pretty-print the anon pid/address info and store that.
		// This won't match any image path, but it's the best we can do.
		else
		{
			result.lib_image = parse_anon(path[i], path[i-1]);
		}

		// skip past the {anon}/pid.addr.add identifier.
		i++;
	}

	// If there's no {cg}/path suffix, we're done.
	if (i == path.size())
		return result;
	
	// Skip the "{cg}" marker (if we're here, we know there must be one).
	++i;

	if (i == path.size() ||
	    (path[i] != "{kern}" && path[i] != "{root}" && ! starts_with(path[i], "{anon")))
	{
		throw invalid_argument("parse_filename() invalid filename, "
				       "{cg} sub-path missing {root}, {kern}, or {anon}: " +
		                       filename);
	}

	// Skip {root}, {kern} or {anon...}, but remember whether {cg} sub-path is {anon}.
	bool cg_anon = (path[i].find("{anon", 0) == 0);
	++i;

	if (! cg_anon) {
		// Collect cg_image pathname, up to end of path.
		for (; i < path.size(); ++i)
			result.cg_image += "/" + path[i];
	}
	else {
		// If this {anon} came from a Java JVM, and OProfile's JVM agent was used,
		// there'll be a pid.jo pseudo-binary containing Java Class.method() symbols.

		// Construct pathname of this psuedo-binary, and see whether it exists.
		// This is a little sneaky:
		// we remove the {dep} pathname and replace it with the {cg} pathname,
		// then as above we truncate and add the ".jo" suffix to find the pid.jo file, if any.
		string::size_type dep_pos = pathname_spec.find("{dep}");
		string::size_type cg_pos  = pathname_spec.find("{cg}");
		string cg_pathname_spec =
			pathname_spec.substr(0, dep_pos+5) +
			pathname_spec.substr(cg_pos+4);

		// Check {anon} identifier for proper format.
		pos = cg_pathname_spec.rfind('.');
		if (pos != string::npos) --pos;
		pos = cg_pathname_spec.rfind('.', pos);
		if (pos == string::npos) {
			throw invalid_argument("parse_filename() invalid filename, "
					       "{cg}/{anon} is not in pid.addr.addr format: " +
					       pathname_spec);
		}

		// If this {anon} came from a Java JVM, and OProfile's JVM agent was used,
		// there'll be a pid.jo pseudo-binary containing Java Class.method() symbols.

		// Construct pathname of this psuedo-binary, and see whether it exists.
		string jitdump = cg_pathname_spec.substr(0, pos) + ".jo";
		if (!stat(jitdump.c_str(), &st))
		{
			// If so, we'll point to this file as if it was the profiled binary image.
			// Later code assumes an optional prefix path
			// is stripped from the lib_image.
			result.cg_image =
				extra_found_images.strip_path_prefix(jitdump);
			// TODO: do we need to add a flag like this?
			//result.cg_jit_dumpfile_exists = true;
		}

		// If not, we pretty-print the anon pid/address info and store that.
		// This won't match any image path, but it's the best we can do.
		else
		{
			result.cg_image = parse_anon(path[i], path[i-1]);
		}
		i++;
	}

	return result;
}

bool parsed_filename::profile_spec_equal(parsed_filename const & parsed)
{
	return 	event == parsed.event &&
		count == parsed.count &&
		unitmask == parsed.unitmask &&
		tgid == parsed.tgid &&
		tid == parsed.tid &&
		cpu == parsed.tid;
}

ostream & operator<<(ostream & out, parsed_filename const & data)
{
	out << data.filename << endl;
	out << data.image << " " << data.lib_image << " "
	    << data.event << " " << data.count << " "
	    << data.unitmask << " " << data.tgid << " "
	    << data.tid << " " << data.cpu << endl;

	return out;
}
