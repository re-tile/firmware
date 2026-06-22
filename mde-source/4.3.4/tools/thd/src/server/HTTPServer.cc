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
// HTTPServer.cc -- HTTP server base class
// Copyright (C) 2010. Tilera Corporation
// =============================================================================

#include "HTTPServer.h"

// C/C++ headers
#include <stdio.h>

// custom headers
#include "utils.h"
#include "collections.h"

// -----------------------------------------------------------------------------
// HTTPServer
// -----------------------------------------------------------------------------

// --- constructors/destructors ---

/** Constructor */
HTTPServer::HTTPServer(const std::string& config_file_path) :
  Server(config_file_path)
{
  initialize();
}

/** Copy constructor */
HTTPServer::HTTPServer(const HTTPServer& server)
{}

/** Assignment operator */
HTTPServer&
HTTPServer::operator=(const HTTPServer& server)
{
  return *this;
}

/** Destructor */
HTTPServer::~HTTPServer()
{
}


// --- init methods ---

/** Initializes members to default values. */
void
HTTPServer::initialize()
{
  m_docroot                = "/etc/tile-thd/html/";
  m_docroot_set            = false;
  m_default_index_filename = "";
  m_access_log             = NULL;
}


// --- accessors ---

/** Gets HTTP server's "docroot" path. */
const Pathname&
HTTPServer::get_docroot() const
{
  return m_docroot;
}

/** Sets HTTP server's "docroot" path. */
void
HTTPServer::set_docroot(const Pathname& path)
{
  m_docroot = path;

  // Note that we've set the docroot explicitly
  // (e.g. from the command line) so we don't override it
  // with whatever is read from the config file.
  m_docroot_set = true;

  // If docroot is relative, absolutize it relative to current directory.
  if (! m_docroot.is_absolute()) {
    m_docroot.make_absolute(Pathname::get_cwd());
  }
}

/** Sets access log file path.
    Can also be "stdout" or "stderr" for the standard IO streams,
    or the empty string, to disable logging.
 */
void
HTTPServer::set_access_log(const std::string& path)
{
  FILE* stream = NULL;
  if (path == "stderr")
    stream = stderr;
  else if (path == "stdout")
    stream = stdout;
  else if (! path.empty())
  {
    stream = fopen(path, "w");
    if (stream == NULL)
    {
      fprintf(stderr, "Could not open access log: %s\n", path.c_str());
    }
  }

  set_access_log(stream);
}

/** Sets access log stream.
    Can be stdout, stderr, or a custom stream,
    or NULL to disable logging. */
void
HTTPServer::set_access_log(FILE* stream)
{
  if (m_access_log != NULL && m_access_log != stdout && m_access_log != stderr)
    fclose(m_access_log);
  m_access_log = stream;
}


// --- methods ---

/** Handles configuration parameter.
    Returns true if parameter is recognized and handled, false if not.
  */
bool
HTTPServer::handle_config_parameter(const std::string& name, const std::string& value)
{
  bool result = false;

  if (name == "docroot")
  {
    // If docroot is already set (i.e. from the command line)
    // don't override with config file parameter.
    if (! m_docroot_set)
      set_docroot(value);
    result = true;
  }
  else if (name == "index_file")
  {
    m_default_index_filename = value;
    result = true;
  }
  else if (name == "path_mapping")
  {
    std::string docroot_path;
    std::string real_path;
    if (split_string(value, "=>", docroot_path, real_path))
    {
      trim(docroot_path);
      trim(real_path);
      m_pathname_mappings.add(Pathname(docroot_path), Pathname(real_path));
      result = true;
    }
  }
  else if (name == "mime_type")
  {
    std::string extension;
    std::string mime_type;
    if (split_string(value, ",", extension, mime_type))
    {
      trim(extension);
      trim(mime_type);
      m_mime_types.put(extension, mime_type);
      result = true;
    }
  }
  else if (name == "exe_path")
  {
    std::string path = value;
    trim(path);
    if (! path.empty())
    {
      m_exe_paths.add(path);
      result = true;
    }
  }
  else if (name == "server_stylesheet")
  {
    // map server-relative path to absolute path
    // since we'll be including the file's content in the generated HTML
    Pathname mapped_path = map_http_path(value);
    if (mapped_path.exists())
      m_server_stylesheet = mapped_path.to_string();
    result = true;
  }
  else if (name == "server_header")
  {
    // map server-relative path to absolute path
    // since we'll be including the file's content in the generated HTML
    Pathname mapped_path = map_http_path(value);
    if (mapped_path.exists())
      m_server_header = mapped_path.to_string();
    result = true;
  }
  else if (name == "server_footer")
  {
    // map server-relative path to absolute path
    // since we'll be including the file's content in the generated HTML
    Pathname mapped_path = map_http_path(value);
    if (mapped_path.exists())
      m_server_footer = mapped_path.to_string();
    result = true;
  }
  else if (name == "access_log")
  {
    set_access_log(value);
    result = true;
  }
  else
  {
    // invoke server method to handle server generic properties
    result = Server::handle_config_parameter(name, value);
  }

  return result;
}

/** Invoked to handle each server connection.
    Note: handler method should NOT close the file descriptor.
    The server will do this when the handler returns.
 */
int
HTTPServer::handle_connection(int fd)
{
  int result = 0;
  log("------------------------------------------------------------\n");
  log("Handling HTTP connection...\n");

  // create request/response objects
  HTTPRequest*  request  = new HTTPRequest();
  HTTPResponse* response = new HTTPResponse();

  // create a context we can break out of
  do
  {
    // --- receive ---

    // receive request data
    log("Receiving HTTP request...\n");
    result = request->receive(fd);
    if (result < 0)
    {
      if (result == -2)
      {
        log("Empty request, ignoring it.\n");
      }
      else if (result == -3)
      {
        log("Incomplete request, ignoring it.\n");
      }
      break;
    }

    // log the client request, if required
    if (m_access_log)
    {
      fprintf(m_access_log, "%li: %s %s %s\n",
              request->get_timestamp(),
              request->get_source().c_str(),
              request->get_method().c_str(),
              request->get_url().to_string().c_str());
      fflush(m_access_log);
    }

    // log request headers, if we're debug logging
    if (m_debug_log != NULL)
      request->fprint(m_debug_log, false);

    // --- process ---

    // let subclass decide how to handle request
    log("Generating HTTP response...\n");
    result = handle_http_request(*request, *response);
    if (result < 0) break;

    // Note: result would only be <0 if we couldn't generate ANY response,
    // not even a 404 response, which is pretty unlikely unless the original
    // request is somehow valid and parsable but nevertheless bogus in some way.

    // --- respond ---

    // send generated response, if any
    log("Sending HTTP response...\n");
    result = response->send(fd);

    // log response headers, if we're debug logging
    // Note: need to do this after sending so we include timestamp
    if (m_debug_log != NULL)
      response->fprint(m_debug_log, false);
  }
  while (false);

  // clean up
  delete request;
  delete response;

  log("Finished handling HTTP connection, result = %i.\n", result);
  log("------------------------------------------------------------\n");

  return result;
}

//
// Here's an outline of the strategy we follow in processing a request:
//
// (1) get the request path from the URL:
//     e.g. from the URL http://host:port/path/name?param1&param2...
//     we extract:                       /path/name
// 
//     This path is of the form "/{name/}*{name}?", and can be thought of
//     as being an "absolute" path, but with the server's docroot directory
//     as the "root" of the file system.
// 
// (2) map the request path to an absolute file-system path, by
//     (a) applying any pathname mappings the user has specified
//         to convert a prefix of the request path to an absolute path
//     (b) failing that, just prepend the docroot directory to the request path
// 
// (3) if the request path doesn't exist, return a 404 error page. [DONE]
//
// (4) if the request path is a directory:
//     - it needs to end in a slash for the browser to handle relative URLs properly,
//       so if it doesn't end in a slash, we emit a redirection response
//       with a URL that has the "correct" path and is otherwise the same. [*] [DONE]
//     - if it _does_ end in a slash:
//       - if the directory contains a default index.html page, return that. [DONE]
//       - otherwise, generate a directory listing page [DONE]
//
//       [*] BTW, this is why we pass the request path around as a string,
//       not a Pathname, because by design the Pathname class discards any
//       trailing slash on a directory path, but in the case of the URL's
//       request path, this trailing slash is needed for correct behavior.
//
// (5) if the request path is not a directory,
//     look at the original request path and see if its prefix matches
//     one of the known "executable" paths -- if so, treat the file as
//     an executable file (i.e. like a CGI-BIN script), run it,
//     and return its output [DONE]
//
// (6) if the file is not in an executable location,
//     treat it as an ordinary file, and return its content with a MIME type
//     based on its file extension, so the browser will display it properly. 
//


/** Invoked to handle each HTTP request. */
int
HTTPServer::handle_http_request(HTTPRequest& request,
                                HTTPResponse& response)
{
  int result = 0;

  // check for HEAD request (headers only)
  response.set_headers_only((request.get_method() == HTTP_MODE_HEAD));

  // get request path (server-relative)
  std::string request_path = request.get_path();
  
  // map it to absolute path
  Pathname path = map_http_path(request_path);

  // check whether mapped path exists at all
  if (! path.exists())
  {
    generate_not_available_response(response, request_path, "File/directory not found or not accessible.");
  }

  // check whether it's a directory
  else if (path.is_directory())
  {
    // if original request path didn't end in '/'
    // issue redirection that adds the '/',
    // so relative hyperlinks from directory listings work properly
    if (! request_path.empty() && ! ends_with("/", request_path))
    {
      // construct new URL with modified path
      // plus original parameters, if any
      std::string redirect_url = request_path + "/";
      if (request.get_url().has_parameters())
        redirect_url += request.get_url().get_parameter_string();

      log("Whoops, directory request must end with a slash, need to redirect to: %s\n",
          redirect_url.c_str());

      // generate HTTP redirect
      generate_redirection_response(response, redirect_url);
    }

    else
    {
      // try adding default index filename to path
      Pathname index_file_path;
      if (! m_default_index_filename.empty())
      {
        index_file_path = Pathname(path, m_default_index_filename);
      }

      // if index file is found
      if (! index_file_path.empty() && index_file_path.exists())
      {
        // return index file
        log ("Found index file in directory, returning it instead: %s\n", index_file_path.c_str());
        generate_file_response(response, request_path, index_file_path);
      }
      else
      {
        // generate directory listing
        generate_directory_response(response, request_path, path);
      }
    }
  }

  // file exists, and is not a directory
  else
  { 
    log ("Checking for executable file: %s\n", request_path.c_str());

    // check whether request docroot path (or a parent path) is declared executable
    bool executable = false;
    FOR_EACH(const_iterator, it, Array<Pathname>, m_exe_paths)
    {
      // Add a slash, to avoid confusion between directories and similarly-named files.
      const std::string& exe_path = (*it).to_string() + "/";
      if (starts_with(exe_path, request_path))
      {
        executable = true;
        break;
      }
    }

    if (executable)
    {
      // generate executable file response
      generate_executable_file_response(response, request_path, path, request.get_url().get_parameters());
    }

    else
    {
      // generate ordinary file response
      generate_file_response(response, request_path, path);
    }
  }

  return result;
}

/** Maps HTTP request path (server-relative)
    to absolute file system path.
  */
Pathname
HTTPServer::map_http_path(const std::string& request_path) const
{
  bool mapped = false;
  Pathname result;

  log("Determining path mapping for: %s\n", request_path.c_str());

  // apply logical path mapping, if we have any
  FOR_EACH_PAIR(const_iterator, it, Map<Pathname COMMA Pathname>, m_pathname_mappings)
  {
    const Pathname& docroot_path = it->first;
    const Pathname& real_path    = it->second;

    Pathname ignore;
    Pathname suffix;

    log("Trying: %s\n", docroot_path.c_str());
    if (Pathname(request_path).split_path_after(docroot_path, ignore, suffix))
    {
      Pathname mapped_path(real_path, suffix);
      log("Mapped path: %s\n", mapped_path.c_str());
      if (mapped_path.exists())
      {
        log("Using mapping: %s => %s\n", docroot_path.c_str(), real_path.c_str());
        result = mapped_path;
        mapped = true;

        // we perform at most one mapping per request
        break;
      }
    }
  }

  if (! mapped && m_docroot != "/")
  {
    log("No mapping found, using docroot: %s\n", m_docroot.c_str());
    result = Pathname(m_docroot, request_path);
  }

  log("Mapped path: %s\n", result.c_str());
  return result;
}


// --- response generation methods ---

/** Generates a response for specified file path. */
void
HTTPServer::generate_file_response(HTTPResponse& response,
                                   const std::string& request_path,
                                   const Pathname& path)
{
  log("This is a file request.\n");
  // test that we can read the file
  FILE* fp = fopen(path, "r");
  if (fp == NULL)
  {
    log("Path could not be read.\n");
    generate_not_available_response(response, request_path, "File not accessible");
  }
  else
  {
    fclose(fp);
    generate_ok_file_response(response, path);
  }
}


/** Generates a response for specified executable file path. */
void
HTTPServer::generate_executable_file_response(HTTPResponse& response,
                                              const std::string& request_path,
                                              const Pathname& path,
                                              const Array<Parameter>& parameters)
{
  log("This is an executable file request.\n");
  // test that we can read the file
  FILE* fp = fopen(path, "r");
  if (fp == NULL)
  {
    log("Path could not be read.\n");
    generate_not_available_response(response, request_path, "File not accessible");
  }
  else
  {
    fclose(fp);
    generate_ok_executable_file_response(response, request_path, path, parameters);
  }
}


/** Invoked to generate an "OK" response that
    returns content of a file.
 */
void
HTTPServer::generate_ok_file_response(HTTPResponse& response,
                                      const Pathname& path)
{
  log("Responding with content of file: %s\n", path.c_str());
  response.set_status(HTTP_STATUS_OK, "OK");

  // Determine content length header from file size
  // Note: pseudo-files (like /proc files) return zero length,
  // so for them we simply omit the header.
  int content_length = path.get_file_size();
  if (content_length > 0)
  {
    response.set_http_header("Content-Length", content_length);
  }

  // Determine content type header from file extension, using declared MIME types.
  // We default to "text/plain" if we can't find any mapping for the file.
  // (Note: we specify utf-8 charset explicitly so browsers can't "guess"
  //  that non-ASCII chars indicate a non-displayable binary file.)
  std::string extension = path.get_extension();
  std::string content_type = m_mime_types.get(extension, "text/plain; charset=utf-8");
  response.set_http_header("Content-Type", content_type);

  response.set_content_path(path);
}


/** Invoked to generate an "OK" response that
    returns content from an executable file.
 */
void
HTTPServer::generate_ok_executable_file_response(HTTPResponse& response,
                                                 const std::string& request_path,
                                                 const Pathname& path,
                                                 const Array<Parameter>& parameters)
{
  // construct command to run
  std::string command;

  // run the command with cwd initially set to the doc root directory,
  // and all output directed to standard out, so we can capture it
  // (We're using popen(), which implicitly wraps this string with "/bin/sh -c '...'")
  command += "cd " + m_docroot.to_string() + " && ";
  command += path.c_str();

  // add parameters from the URL
  FOR_EACH(const_iterator, it, Array<Parameter>, parameters)
  {
    const Parameter& p = *it;
    command += " " + p.get_name();
    if (p.has_value())
    {
      command += "=" + p.get_value();
    }
  }

  command += " < /dev/null 2>&1";

  log("Executing command: %s\n", command.c_str());
  FILE *pp = popen(command.c_str(), "r");
  if (pp == NULL)
  {
    log("Execution of command failed: %s\n", command.c_str());
    generate_not_available_response(response, request_path, "File not executable");
  }
  else
  {
    log("Responding with content of executable file: %s\n", path.c_str());
    response.set_status(HTTP_STATUS_OK, "OK");
    // 404 response may be ephemeral, so browser should not cache it
    response.set_http_header("Cache-Control", "no-cache, no-store");
    std::string line;

    // read CGI-BIN headers, up to first blank line
    while (readline(pp, line) > 0)
    {
      std::string name;
      std::string value;
      if (split_string(line, ":", name, value))
      {
        trim(name);
        trim(value);
      }
      response.add_http_header(name, value);
    }

    // read rest of output as file content
    while (readline(pp, line) >= 0)
    {
      response.add_content(line + "\n");
    }

    // must use pclose() to close a popen() stream!
    pclose(pp);
    log("Finished responding with content of executable file: %s\n", path.c_str());
  }
}


/** Invoked to generate a 302 response that indicates
    a redirection of the request to a different path.
 */
void
HTTPServer::generate_redirection_response(HTTPResponse& response,
                                          const std::string& redirect_url,
                                          const std::string& reason)
{
  log("Redirecting to: %s.\n", redirect_url.c_str());
  response.set_status(HTTP_STATUS_REDIRECT, reason);
  response.set_http_header("Content-Type",  "text/html");
  response.set_http_header("Location", redirect_url);
  // redirection response may be ephemeral, so browser should not cache it
  response.set_http_header("Cache-Control", "no-cache, no-store");

  generate_standard_html_prefix(response, "Pathname Redirection");

  response.printf("<h1>Redirecting...</h1>\n");
  response.printf("<p>Redirecting to URL:</p>\n");
  response.printf("<code>%s</code>\n", redirect_url.c_str());

  generate_standard_html_suffix(response);

  response.printf("</body></html>\n");
}


/** Invoked to generate a 404 response that indicates
    request path could not be satisfied.
 */
void
HTTPServer::generate_not_available_response(HTTPResponse& response,
                                            const std::string& request_path,
                                            const std::string& reason)
{
  log("Responding '%s' for: %s\n", reason.c_str(), request_path.c_str());
  response.set_status(HTTP_STATUS_NOT_AVAILABLE, reason);
  response.set_http_header("Content-Type",  "text/html");
  // 404 response may be ephemeral, so browser should not cache it
  response.set_http_header("Cache-Control", "no-cache, no-store");

  generate_standard_html_prefix(response, 
                                "" + to_string(response.get_status()) + " -- " + reason.c_str());

  response.printf("<p>Sorry, could not return resource for requested path:</p>\n");
  response.printf("<code>%s</code>\n", request_path.c_str());

  generate_standard_html_suffix(response);

  response.printf("</body></html>\n");
}


/** Generates a directory listing page for specified directory path. */
void
HTTPServer::generate_directory_response(HTTPResponse& response,
                                        const std::string& request_path,
                                        const Pathname& pathname)
{
  log("Responding with directory listing for: %s\n", pathname.c_str());
  Array<Pathname> pathnames;
  int count = pathname.directory_list(pathnames);
  if (count < 0)
  {
    generate_not_available_response(response, request_path, "Directory not accessible");
    return;
  }

  response.set_status(HTTP_STATUS_OK, "OK");
  response.set_http_header("Content-Type",  "text/html");
  // directory listings are ephemeral, so browser should not cache them
  response.set_http_header("Cache-Control", "no-cache, no-store");

  generate_standard_html_prefix(response, std::string("Directory: ") + request_path.c_str());

  response.printf("<ul>\n");
  FOR_EACH(const_iterator, it, Array<Pathname>, pathnames)
  {
    const Pathname& p = *it;
    std::string pathname = p.to_string();
    bool is_directory    = p.is_directory();
    const char* slash_if_any   = is_directory ? "/" : "";

    // Note: we use request_path, not path, for hyperlinks
    // since we might be using path mapping for this directory,
    // and we want the link to display the unmapped path
    response.printf("<li class=\"single\">");
    response.printf("<a href=\"%s%s%s\">%s%s</a>", 
                    request_path.c_str(), pathname.c_str(), slash_if_any,
                    pathname.c_str(), slash_if_any);
    response.printf("</li>\n");
  }
  response.printf("</ul>\n");

  generate_standard_html_suffix(response);

  response.printf("</body></html>\n");
}

/** Generates standard HTML prefix content for generated pages. */
void
HTTPServer::generate_standard_html_prefix(HTTPResponse& response,
                                          const std::string& title)
{
  // Copyright header
  response.printf("<!-- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -->\n");
  response.printf("<!-- Copyright (C) 2012, Tilera Corporation. All rights reserved.        -->\n");
  response.printf("<!-- Use is subject to license terms.                                    -->\n");
  response.printf("<!-- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -->\n");

  // HTML "strict" DTD
  response.printf("\n");
  response.printf("<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\"\n");
  response.printf("  \"http://www.w3.org/TR/html4/strict.dtd\">\n");

  // Headers
  response.printf("\n");
  response.printf("<html><head>\n");

  response.printf("\n");
  response.printf("<!-- stylesheets -->\n");
  response.printf("<link rel=\"stylesheet\" type=\"text/css\" href=\"/include/themes/tilera.css\" />\n");

  response.printf("\n");
  response.printf("<!-- scripts -->\n");
  response.printf("<script type=\"text/javascript\" src=\"/include/scripts/core.js\"></script>\n");

  if (! m_server_stylesheet.empty())
  {
    response.printf("\n");
    response.printf("<!-- server stylesheets/scripts -->\n");
    response.add_file_content(m_server_stylesheet);
  }

  response.printf("\n");
  response.printf("<!-- set page title -->\n");
  response.printf("<script type=\"text/javascript\">setHTMLTitle(\"%s\");</script>\n", title.c_str());

  response.printf("\n");
  response.printf("</head><body class='tilera'>\n");

  response.printf("<!-- server prefix -->\n");
  response.add_file_content(map_http_path("/include/server_body_prefix.html"));

  // add server content header, if any
  if (! m_server_header.empty())
  {
    response.add_file_content(m_server_header);
  }

  // Insert title, if any
  if (! title.empty())
  {
    response.printf("\n");
    response.printf("<!-- show page title -->\n");
    response.printf("<h1><script type=\"text/javascript\">getHTMLTitle();</script></h1>\n");
  }
}

/** Generates standard HTML suffix content for generated pages. */
void
HTTPServer::generate_standard_html_suffix(HTTPResponse& response)
{
  // add server content header, if any
  if (! m_server_footer.empty())
  {
    response.add_file_content(m_server_footer);
  }
}
