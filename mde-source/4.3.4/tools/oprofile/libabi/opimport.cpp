/**
 * @file opimport.cpp
 * Import sample files from other ABI
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author Graydon Hoare
 */

#include "abi.h"
#include "odb.h"
#include "popt_options.h"
#include "op_sample_file.h"

#include <fstream>
#include <iostream>
#include <vector>
#include <cassert>
#include <cstring>
#include <cstdlib>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/mman.h>
#include <cstdlib>
#include <cstring>

using namespace std;

namespace {
	string output_filename;
	string abi_filename;
	bool verbose;
	bool force;
};


popt::option options_array[] = {
	popt::option(verbose, "verbose", 'V', "verbose output"),
	popt::option(output_filename, "output", 'o', "output to file/dir", "filename"),
	popt::option(abi_filename, "abi", 'a', "abi description", "filename"),
	popt::option(force, "force", 'f', "force conversion, even if identical")
};


struct extractor {

	abi const & theabi;

	unsigned char const * begin;
	unsigned char const * end;
	bool little_endian;

	explicit
	extractor(abi const & a, unsigned char const * src, size_t len)
		: theabi(a), begin(src), end(src + len) {
		little_endian = theabi.need(string("little_endian")) == 1;
		if (verbose) {
			cerr << "source byte order is: "
			     << string(little_endian ? "little" : "big")
			     << " endian" << endl;
		}
	}

	template <typename T>
	void extract(T & targ, void const * src_,
	             char const * sz, char const * off);
};


template <typename T>
void extractor::extract(T & targ, void const * src_,
                        char const * sz, char const * off)
{
	unsigned char const * src = static_cast<unsigned char const *>(src_)
		+ theabi.need(off);
	size_t nbytes = theabi.need(sz);
	size_t real_nbytes;
	
	if (nbytes == 0)
		return;
	
	assert(src >= begin);
	assert(src + nbytes <= end);

	real_nbytes = min(nbytes, sizeof(T));
	
	if (verbose)
		cerr << hex << "get " << sz << " = " << real_nbytes
		     << " bytes @ " << off << " = " << (src - begin)
		     << " : ";

	targ = 0;
	if (little_endian)
		while(real_nbytes--)
			targ = (targ << 8) | src[real_nbytes];
	else
		for(size_t i = nbytes - real_nbytes; i < nbytes; ++i)
			targ = (targ << 8) | src[i];
	
	if (verbose)
		cerr << " = " << targ << endl;
}


void import_from_abi(abi const & abi, void const * srcv,
                     size_t len, odb_t * dest) throw (abi_exception)
{
	struct opd_header * head =
		static_cast<opd_header *>(odb_get_data(dest));
	unsigned char const * src = static_cast<unsigned char const *>(srcv);
	unsigned char const * const begin = src;
	extractor ext(abi, src, len);	

	memcpy(head->magic, src + abi.need("offsetof_header_magic"), 4);

	// begin extracting opd header
	ext.extract(head->version, src, "sizeof_u32", "offsetof_header_version");
	ext.extract(head->cpu_type, src, "sizeof_u32", "offsetof_header_cpu_type");
	ext.extract(head->ctr_event, src, "sizeof_u32", "offsetof_header_ctr_event");
	ext.extract(head->ctr_um, src, "sizeof_u32", "offsetof_header_ctr_um");
	ext.extract(head->ctr_count, src, "sizeof_u32", "offsetof_header_ctr_count");
	ext.extract(head->is_kernel, src, "sizeof_u32", "offsetof_header_is_kernel");

	//Hack for tile. Since x86 and tile have same "double" format.
	u64 cpu_speed_u64;
	ext.extract(cpu_speed_u64, src, "sizeof_double", "offsetof_header_cpu_speed");
	head->cpu_speed = *((double *) &cpu_speed_u64);

	ext.extract(head->mtime, src, "sizeof_time_t", "offsetof_header_mtime");
	ext.extract(head->cg_to_is_kernel, src, "sizeof_u32",
		"offsetof_header_cg_to_is_kernel");
	ext.extract(head->anon_start, src, "sizeof_u32",
		"offsetof_header_anon_start");
	ext.extract(head->cg_to_anon_start, src, "sizeof_u32",
		"offsetof_header_cg_to_anon_start");
	src += abi.need("sizeof_struct_opd_header");
	// done extracting opd header

	// begin extracting necessary parts of descr
	odb_node_nr_t node_nr;
	ext.extract(node_nr, src, "sizeof_odb_node_nr_t", "offsetof_descr_current_size");
	src += abi.need("sizeof_odb_descr_t");
	// done extracting descr

	// skip node zero, it is reserved and contains nothing usefull
	src += abi.need("sizeof_odb_node_t");

	// begin extracting nodes
	unsigned int step = abi.need("sizeof_odb_node_t");
	if (verbose)
		cerr << "extracting " << node_nr << " nodes of " << step << " bytes each " << endl;

	assert(src + (node_nr * step) <= begin + len);

	for (odb_node_nr_t i = 1 ; i < node_nr ; ++i, src += step) {
		odb_key_t key;
		odb_value_t val;
		ext.extract(key, src, "sizeof_odb_key_t", "offsetof_node_key");
		ext.extract(val, src, "sizeof_odb_value_t", "offsetof_node_value");
		int rc = odb_add_node(dest, key, val);
		if (rc != EXIT_SUCCESS) {
			cerr << strerror(rc) << endl;
			exit(EXIT_FAILURE);
		}
	}
	// done extracting nodes
}


void copy_file(const char * file_out, const char * file_in)
{
	int in_fd, out_fd;
	int len;
	char buf[1024];

	if (!strcmp(file_out, file_in))
		return;

	assert((in_fd = open(file_in, O_RDONLY)) > 0);		
	assert((out_fd = open(file_out, O_WRONLY | O_CREAT)) > 0);		

	while ((len = read(in_fd, buf, sizeof(buf))) > 0)
		write(out_fd, buf, len);

	close(out_fd);
	close(in_fd);
}

void inline cleanup_file(const char * file)
{
	int fd;
	
	assert ((fd = open(file, O_WRONLY | O_TRUNC)) > 0);
	close(fd);
}


int import_file(const char * file_out, const char * file_in,
		abi input_abi)
{
	int in_fd;
	struct stat statb;
	void * in;
	odb_t dest;
	int rc;
	static char tmpfile[] = "/tmp/opimport-XXXXXX";
	static int tmpfile_exist;

	// In-place importing 
	if (!strcmp(file_out, file_in)) {
		if (!tmpfile_exist) {
			int tmp_fd;
			assert ((tmp_fd = mkstemp(tmpfile)) > 0);
			close(tmp_fd);
			tmpfile_exist = 1;

		}
		copy_file(tmpfile, file_in);
		cleanup_file(file_in);
		assert((in_fd = open(tmpfile, O_RDONLY)) > 0);		
	} else {
		assert((in_fd = open(file_in, O_RDONLY)) > 0);		
	}

	assert(fstat(in_fd, &statb) == 0);
	assert((in = mmap(0, statb.st_size, PROT_READ,
			  MAP_PRIVATE, in_fd, 0)) != (void *)-1);

	rc = odb_open(&dest, file_out, ODB_RDWR,
		      sizeof(struct opd_header));
	if (rc) {
		cerr << "odb_open() fail:\n"
		     << strerror(rc) << endl;
		exit(EXIT_FAILURE);
	}

	try {
		import_from_abi(input_abi, in, statb.st_size, &dest);
	} catch (abi_exception & e) {
		cerr << "caught abi exception: " << e.desc << endl;
	}

	odb_close(&dest);

	assert(munmap(in, statb.st_size) == 0);

	close(in_fd);

	return 0;
}


int import_dir(const char * dir_out, const char * dir_in,
	       abi input_abi)
{
	DIR * dir;
	struct dirent * de;
	char file_out[PATH_MAX];
	char file_in[PATH_MAX];

	if (strcmp(dir_out, dir_in))
	{
		// Create the output directory if needed.  Will not check the
		// R/W permission or whether the output directory has existed
		// as a non-directory. Leave to following code to deal with
		// such cases.
		if (access(dir_out, F_OK) && mkdir(dir_out, S_IRWXU)) {
			cerr << "create directory '%s' fails:\n"
			     << dir_out << endl;
			exit(EXIT_FAILURE);
		}
	}

	dir = opendir(dir_in);
	if (dir == NULL)
	{
		cerr << "open directory '%s' fails:\n"
		     << dir_in << endl;
		exit(EXIT_FAILURE);
	}

	while ((de = readdir(dir)) != NULL)
	{
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;

		snprintf(file_out, sizeof(file_out), "%s/%s", dir_out,
			 de->d_name);
		file_out[sizeof(file_out) - 1] = 0;

		snprintf(file_in, sizeof(file_in), "%s/%s", dir_in, de->d_name);
		file_in[sizeof(file_in) - 1] = 0;

		struct stat fs;

		stat(file_in, &fs);
		if (S_ISDIR(fs.st_mode))
			import_dir(file_out, file_in, input_abi);
		else if (strchr(file_in, '{'))
			// Hack: '{' means directory includes sample files. We can
			// detect through header file or something else, but still
			// not 100% reliable.
			import_file(file_out, file_in, input_abi);
		else
			//Simply copy other kind of files.
			copy_file(file_out, file_in);
	}

	closedir(dir);
	return 0;
}


int main(int argc, char const ** argv)
{
	struct stat fs;
	vector<string> inputs;
	popt::parse_options(argc, argv, inputs);

	if (inputs.size() != 1) {
		cerr << "error: must specify exactly 1 input file" << endl;
		exit(1);
	}

	abi current_abi, input_abi;

	{
		ifstream abi_file(abi_filename.c_str());
		if (!abi_file) {
			cerr << "error: cannot open abi file "
			     << abi_filename << endl;
			exit(1);
		}
		abi_file >> input_abi;
	}

	if (!force && current_abi == input_abi) {
		cerr << "input abi is identical to native. "
		     << "no conversion necessary." << endl;
		exit(1);
	}

	stat(inputs[0].c_str(), &fs);
	if (S_ISDIR(fs.st_mode)) {
		return import_dir(output_filename.c_str(), inputs[0].c_str(),
				  input_abi);
	} else {
		return import_file(output_filename.c_str(), inputs[0].c_str(),
				   input_abi);
	}

	return 0;
}

