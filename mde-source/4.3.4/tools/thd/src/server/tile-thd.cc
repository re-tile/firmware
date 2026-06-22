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
// thd.cc -- "thd" HTTP server
// Copyright (C) 2010. Tilera Corporation
// =============================================================================

// C/C++ includes.
#include <stdio.h>
#include <string>

// Custom includes.
#include "HTTPServer.h"
#include "utils.h"
#include "string_utils.h"


// -----------------------------------------------------------------------------
// main()
// -----------------------------------------------------------------------------

/** Main function. */
int main(int argc, char** argv)
{
  // Exit status.
  int status = 0;

  // Command line options.
  bool usage = false;
  bool debug = false;
  bool test = false;
  Pathname config_file("");
  std::string address;
  int port = -1;
  Pathname docroot(""); // default pathname is "/"

  // Process command line.
  for (int i=1; i<argc; ++i)
  {
    char* arg = argv[i];
    if (streql("-h", arg) || streql("--help", arg))
    {
      usage = true;
    }
    else if (streql("--debug", arg))
    {
      debug = true;
    }
    else if (i<(argc-1) && (streql("-c", arg) || streql("--config", arg)))
    {
      config_file = argv[++i];
    }
    else if (i<(argc-1) && (streql("-a", arg) || streql("--address", arg)))
    {
      address = argv[++i];
    }
    else if (i<(argc-1) && (streql("-p", arg) || streql("--port", arg)))
    {
      port = to_int(argv[++i]);
    }
    else if (i<(argc-1) && (streql("-d", arg) || streql("--docroot", arg)))
    {
      docroot = argv[++i];
    }
    else if (streql("-t", arg) || streql("--test", arg))
    {
      test = true;
    }
    else
    {
      fprintf(stderr, "Unrecognized argument: %s\n", arg);
      usage = true;
      status = -1;
    }
  }

  // Display usage and exit, if needed.
  if (usage)
  {
    fprintf(stderr, "tile-thd HTTP server\n");
    fprintf(stderr, "Usage: tile-thd\n");
    fprintf(stderr, "  {-c|--config path}\n");
    fprintf(stderr, "  {-a|--address address}\n");
    fprintf(stderr, "  {-p|--port port}\n");
    fprintf(stderr, "  {-d|--docroot path}\n");
    fprintf(stderr, "  {-h|--help}\n");
    fprintf(stderr, "  {--debug}\n");
    fprintf(stderr, "  -c|--config path     -- "
            "Specifies location of config file.\n");
    fprintf(stderr, "  -a|--address address -- "
            "Specifies interface address, overriding config file.\n");
    fprintf(stderr, "  -p|--port port       -- "
            "Specifies port number, overriding config file.\n");
    fprintf(stderr, "  -d|--docroot path    -- "
            "Specifies HTML docroot path, overriding config file.\n");
    fprintf(stderr, "  -h|--help            -- "
            "Displays this help text and exits.\n");
    fprintf(stderr, "  --debug              -- "
            "Runs in debug mode (run in foreground with debug log to stderr).\n");

    exit(status);
  }

  if (test)
  {
    Pathname a("");
    Pathname b("/");
    Pathname c("/foo");
    Pathname d("/foo/");
    Pathname e("/foo/bar");
    Pathname f("/foo/bar/");
    Pathname g("/foo/bar/baz");

    printf("a = %s\n", a.to_string().c_str());
    printf("b = %s\n", b.to_string().c_str());
    printf("c = %s\n", c.to_string().c_str());
    printf("d = %s\n", d.to_string().c_str());
    printf("e = %s\n", e.to_string().c_str());
    printf("f = %s\n", f.to_string().c_str());
    printf("g = %s\n", g.to_string().c_str());

    status = 0;

    return status;
  }


  // Create server.
  HTTPServer s;

  // When running with --debug flag, set debug log to stderr early,
  // so we capture output from config file.
  // Note: this could be overridden by debug_log in config file,
  // but you wouldn't normally be using both at the same time.
  if (debug)
  {
    s.set_access_log(stderr);
    s.set_debug_log(stderr);
  }

  // Apply command-line docroot override early,
  // so it's used properly in looking up other properties.
  if (! docroot.empty())
  {
    s.set_docroot(docroot);
    printf("Set docroot to: %s\n", docroot.c_str());
  }

  // Load config file.
  s.load_config_file(config_file);

  // Apply command-line overrides.
  s.set_port(port);
  if (! address.empty())
    s.set_address(address);

  // Handle "debug mode" request.
  if (debug)
  {
    s.set_daemonize(false);
    s.set_fork(false);
    s.set_debug_log(stderr);
  }

  // Start the server and let it run.
  status = s.run();

  // We're done.
  return status;
}
