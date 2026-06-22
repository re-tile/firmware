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

// =============================================================================
// HTTPServer.h -- HTTP server base class
// Copyright (C) 2010. Tilera Corporation
// =============================================================================

// Multiple-inclusion guard.
#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

// C includes
#include <stdio.h>      // printf, etc.
#include <stdlib.h>

// C++ includes
#include <string>       // string

// custom includes
#include "Server.h"
#include "HTTPRequest.h"
#include "HTTPResponse.h"
#include "Pathname.h"


// -----------------------------------------------------------------------------
// HTTPServer
// -----------------------------------------------------------------------------

/** HTTP server (base class) */
class HTTPServer : public Server
{
  // --- members ---
protected:
  /** HTTP server's "docroot" directory. */
  Pathname m_docroot;

  /** Whether docroot has been set explicitly. */
  bool m_docroot_set;

  /** Default directory index filename. */
  std::string m_default_index_filename;

  /** Pathname mappings. */
  Map<Pathname, Pathname> m_pathname_mappings;

  /** Mapping from file extension to MIME type. */
  Map<std::string, std::string> m_mime_types;

  /** Executable paths, if any. */
  Array<Pathname> m_exe_paths;

  /** Access log stream. */
  FILE* m_access_log;

  /** Server stylesheet/script file location (URL fragment). */
  std::string m_server_stylesheet;

  /** Server content header file location (URL fragment). */
  std::string m_server_header;

  /** Server content footer file location (URL fragment). */
  std::string m_server_footer;


  // --- constructors/destructors ---
public:
  /** Constructor */
  HTTPServer(const std::string& config_file_path = "");

protected:
  /** Copy constructor */
  HTTPServer(const HTTPServer& server);

  /** Assignment operator */
  HTTPServer&
  operator=(const HTTPServer& server);

public:
  /** Destructor */
  ~HTTPServer();


  // --- init methods ---
protected:
  /** Initializes members to default values. */
  void
  initialize();


  // --- accessors ---
public:
  /** Gets HTTP server's "docroot" path. */
  const Pathname&
  get_docroot() const;

  /** Sets HTTP server's "docroot" path. */
  void
  set_docroot(const Pathname& path);

  /** Sets access log file path.
      Can also be "stdout" or "stderr" for the standard IO streams,
      or the empty string, to disable logging.
   */
  void
  set_access_log(const std::string& path);

  /** Adds mapping from filename extension (".xyz")
      to MIME type. ("text/plain") */
  void
  add_mime_type(const std::string& extension,
		const std::string& mime_type);

  /** Gets mapping, if any, from filename extension (".xyz") 
      to MIME type. ("text/plain")
      If no mapping is found, returns specified default value.
   */
  const std::string
  get_mime_type(const std::string& extension,
		const std::string& default_value) const;

  /** Sets access log stream.
      Can be stdout, stderr, or a custom stream,
      or NULL to disable logging. */
  void
  set_access_log(FILE* stream);


  // --- methods ---
public:
  /** Handles configuration parameter.
      Returns true if parameter is recognized and handled, false if not.
    */
  virtual bool
  handle_config_parameter(const std::string& name, const std::string& value);

  /** Invoked to handle each connection.
      Note: handler method should NOT close the file descriptor.
      The server will do this when the handler returns.
   */
  virtual int
  handle_connection(int fd);


  /** Invoked to handle each HTTP request. */
  int
  handle_http_request(HTTPRequest& request,
		      HTTPResponse& response);


  /** Maps HTTP request path (server-relative)
      to absolute file system path.
    */
  Pathname
  map_http_path(const std::string& request_path) const;


  // --- response generation methods ---

  /** Generates a response for specified file path. */
  void
  generate_file_response(HTTPResponse& response,
			 const std::string& request_path,
			 const Pathname& path);

  /** Generates a response for specified executable file path. */
  void
  generate_executable_file_response(HTTPResponse& response,
				    const std::string& request_path,
				    const Pathname& path,
				    const Array<Parameter>& parameters);


  /** Invoked to generate an "OK" response that
      returns content of a file stream.
   */
  void
  generate_ok_file_response(HTTPResponse& response,
			    const Pathname& path);

  /** Invoked to generate an "OK" response that
      returns content from an executable file.
   */
  void
  generate_ok_executable_file_response(HTTPResponse& response,
				       const std::string& request_path,
				       const Pathname& path,
				       const Array<Parameter>& parameters);

  /** Invoked to generate a 302 response that indicates
      a redirection of the request to a different path.
   */
  void
  generate_redirection_response(HTTPResponse& response,
				const std::string& redirect_url,
				const std::string& reason = "Redirection");

  /** Invoked to generate a 404 response that indicates
      requested path could not be satisfied.
   */
  void
  generate_not_available_response(HTTPResponse& response,
				  const std::string& request_path,
				  const std::string& reason = "Not Available");

  /** Generates a response for specified directory path. */
  void
  generate_directory_response(HTTPResponse& response,
			      const std::string& request_path,
			      const Pathname& path);

  /** Generates standard HTML prefix content for generated pages. */
  void
  generate_standard_html_prefix(HTTPResponse& response,
                                const std::string& title);

  /** Generates standard HTML suffix content for generated pages. */
  void
  generate_standard_html_suffix(HTTPResponse& response);

};

// Multiple-inclusion guard.
#endif
