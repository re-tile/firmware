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
// Server.cc -- socket-based server
// Copyright (C) 2010. Tilera Corporation
// =============================================================================

// C/C++ includes.
#include <stdio.h>      // printf, etc.
#include <stdlib.h>
#include <stdarg.h>     // va_arg() (variable-length argument lists)
#include <errno.h>      // errno, strerror()

#include <unistd.h>     // fork, close, etc.
#include <sys/stat.h>   // umask, stat
#include <fcntl.h>      // open
#include <string.h>     // memset

#include <netinet/in.h> // sockets
#include <sys/types.h>  // sockets
#include <sys/socket.h> // sockets
#include <signal.h>     // signal, SIGCHLD, etc.

// custom includes
#include "Server.h"
#include "utils.h"
#include "string_utils.h"


// -----------------------------------------------------------------------------
// Server
// -----------------------------------------------------------------------------

// --- constructors/destructors ---

/** Constructor. */
Server::Server(const Pathname& config_file_path)
{
  initialize(config_file_path);
}

/** Copy constructor. */
Server::Server(const Server& server)
{}

/** Assignment operator. */
Server&
Server::operator==(const Server& server)
{
  return *this;
}

/** Destructor. */
Server::~Server()
{
}


// --- init methods ---

/** Initializes members to default values. */
void
Server::initialize(const Pathname& config_file_path)
{
  m_address = "";
  m_port = DEFAULT_PORT;
  m_pid_file = "";
  m_config_file_path = "";
  set_config_file(config_file_path);
  m_daemonize = true;
  m_fork = true;
  m_debug_log = NULL;
}

/** Cleans up members. */
void
Server::cleanup()
{
  set_debug_log(NULL);
  if (m_socket_fd >= 0)
  {
    close(m_socket_fd);
    m_socket_fd = -1;
  }
}


// --- accessors ---

/** Sets configuration file path. */
void
Server::set_config_file(const Pathname& config_file_path)
{
  m_config_file_path =
    (! config_file_path.empty()) ? config_file_path : DEFAULT_CONF_PATH;

  // If docroot is relative, absolutize it relative to current directory.
  m_config_file_path.make_absolute(Pathname::get_cwd());
}

/** Sets interface address to bind to. */
void
Server::set_address(const std::string& address)
{
  m_address = address;
}

/** Sets port to bind to.
    If port is <0, port is set to default (DEFAULT_PORT). */
void
Server::set_port(int port)
{
  m_port = (port >= 0) ? port : DEFAULT_PORT;
}

/** Sets whether server runs as background daemon. */
void
Server::set_daemonize(bool daemonize)
{
  m_daemonize = daemonize;
}

/** Sets whether server forks a child for each request. */
void
Server::set_fork(bool fork)
{
  m_fork = fork;
}

/** Sets PID file path.
    Can also be the empty string, to disable the PID file.
 */
void
Server::set_pid_file(const Pathname& pathname)
{
  m_pid_file = pathname;
}

/** Sets debug log file path.
    Can also be "stdout" or "stderr" for the standard IO streams,
    or the empty string, to disable logging.
 */
void
Server::set_debug_log(const Pathname& pathname)
{
  FILE* stream = NULL;
  if (pathname == "stderr")
    stream = stderr;
  else if (pathname == "stdout")
    stream = stdout;
  else if (! pathname.empty())
  {
    stream = fopen(pathname, "w");
    if (stream == NULL)
    {
      fprintf(stderr, "Could not open debug log: %s\n", pathname.c_str());
    }
  }

  set_debug_log(stream);
}

/** Sets debug log stream.
    Can be stdout, stderr, or a custom stream,
    or NULL to disable logging. */
void
Server::set_debug_log(FILE* stream)
{
  if (m_debug_log != NULL && m_debug_log != stdout && m_debug_log != stderr)
    fclose(m_debug_log);
  m_debug_log = stream;
}


// --- config file methods ---

/** Loads configuration file. */
void
Server::load_config_file(const Pathname& config_file_path)
{
  // if we're setting config file path here, capture it,
  // else use the already-configured default
  if (! config_file_path.empty())
    set_config_file(config_file_path);

  // NOTE: until config file has had a chance to set the log path,
  // we'll log to stderr so problems locating the config file are visible

  if (! m_config_file_path.exists())
  {
    log(stderr, "Could not find config file at: %s\n", m_config_file_path.c_str());
    exit(-1);
  }

  FILE* config = fopen(m_config_file_path, "r");
  if (config == NULL)
  {
    log(stderr, "Could not open config file at: %s\n", m_config_file_path.c_str());
    exit(-1);
  }

  bool logged_file_path = false;

  // NOTE: we can start calling normal log() now, since one of the first items
  // in the config file should be the debug_log option, if it's set at all

  std::string line;
  while (readline(config, line) >= 0)
  {
    if (line.empty() || line[0] == '\n' || line[0] == '#')
    {
      // skip blank lines and comment lines
      continue;
    }

    std::string name;
    std::string value;
    if (split_string(line, ":", name, value))
    {
      trim(name);
      trim(value);

      // dispatch to overridable method
      // this allows server subclasses to handle their own parameters
      bool okay = handle_config_parameter(name, value);

      if (okay)
      {
        if (! logged_file_path)
        {
          log("Loading configuration file: %s\n", m_config_file_path.c_str());
          logged_file_path = true;
        }
        log("- %s: \t'%s'\n", name.c_str(), value.c_str());
      }
      else
      {
        log("Unrecognized config parameter ignored: "
            "%s: %s\n",
            name.c_str(),
            value.c_str());
      }
    }
  }

  log("Finished loading configuration file: %s\n", m_config_file_path.c_str());
}

/** Handles configuration parameter.
    Returns true if parameter is recognized and handled, false if not.
    Intended to be overridden by subtypes; overriding method
    can call superclass method to handle generic server properties.
  */
bool
Server::handle_config_parameter(const std::string& name, const std::string& value)
{
  bool result = false;

  if (name == "address")
  {
    set_address(value);
    result = true;
  }
  else if (name == "port")
  {
    set_port(to_int(value));
    result = true;
  }
  else if (name == "daemonize")
  {
    set_daemonize(value == "true" || value == "yes" || value == "1");
    result = true;
  }
  else if (name == "fork")
  {
    set_fork(value == "true" || value == "yes" || value == "1");
    result = true;
  }
  else if (name == "pid_file")
  {
    set_pid_file(value);
    result = true;
  }
  else if (name == "debug_log")
  {
    set_debug_log(value);
    result = true;
  }

  return result;
}


// --- methods ---

/** Runs server. */
int
Server::run()
{
  int result = 0;

  log("Server started.\n");

  // create server-side socket
  m_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (m_socket_fd < 0)
  {
    log("Could not create server socket, exiting.\n");
    log_errno();
    fprintf(stderr, "Could not create server socket, exiting.\n");
    exit(-1);
  }

  // enable immediate reuse of socket port number
  int yes = 1;
  setsockopt(m_socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  // specify interface(s)/port(s) we'll bind to
  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;     // internet connection
  server_addr.sin_port = htons(m_port); // on specified port
  server_addr.sin_addr.s_addr = (m_address.empty()) ?
    INADDR_ANY :        // any interface
    inet_addr(m_address.c_str()); // specified interface

  // bind the socket
  if (bind(m_socket_fd,
           (struct sockaddr *) &server_addr,
           sizeof(server_addr))
      < 0)
  {
    log("Could not bind to port %i, exiting.\n", m_port);
    log_errno();
    fprintf(stderr, "Could not bind to port %i, exiting.\n", m_port);
    exit(-1);
  }

  // start listening
  if (listen(m_socket_fd, 5) < 0)
  {
    log("Could not listen on port %i, exiting.\n", m_port);
    log_errno();
    fprintf(stderr, "Could not listen on port %i, exiting.\n", m_port);
    exit(-1);
  }

  if (! m_address.empty())
  {
    fprintf(stdout, "Server listening on %s:%i.\n", m_address.c_str(), m_port);
  }
  else
  {
    fprintf(stdout, "Server listening on port %i.\n", m_port);
  }

  // Make sure any prior output is flushed, otherwise
  // we can get duplicate output from forked children.
  fflush(stdout);
  fflush(stderr);

  // Now that we know we have a working listener socket,
  // run as a daemon process, unless we were told not to.
  if (m_daemonize) daemonize();

  // NOTE: from this point on we need to write any error messages
  // to log file, since daemonize closes stdin/out/err.

  // write pid to file, if specified
  if (! m_pid_file.empty())
  {
    FILE* pf = fopen(m_pid_file, "w");
    if (pf == NULL)
    {
      log("Could not write pid to file: %s\n", m_pid_file.c_str());
      log_errno();
    }
    else
    {
      fprintf(pf, "%i", getpid());
      fclose(pf);
    }
  }

  // ignore SIGCHLD from forked processes, allowing them to terminate,
  // so they aren't left behind as Z (zombie) processes
  __sighandler_t sigchld = signal(SIGCHLD, SIG_IGN);

  // prepare to receive client connections
  struct sockaddr_in client_addr;
  socklen_t client_len = sizeof(client_addr);
  memset(&client_addr, 0, client_len);

  // for now, iterate indefinitely until we're killed
  // TODO: configure sockets so accept call times out,
  // and listen for some signal so we can exit gracefully
  while (1)
  {
    // accept a connection (blocking call)
    log("Server waiting for connection...\n");
    int connection_fd = accept(m_socket_fd, 
                               (struct sockaddr *) &client_addr,
                               &client_len);

    // when we get a connection
    if (connection_fd >= 0)
    {
      log("Connection received by server, fd = %i.\n", connection_fd);

      // if server process handles the connection...
      if (! m_fork)
      {
        // restore original child handling,
        // since we're not forking
        signal(SIGCHLD, sigchld);

        // delegate to connection handler method
        log("Delegating to handler method...\n");
        result = handle_connection(connection_fd);
        log("Handler method returned %i.\n", result);

        // close it
        close(connection_fd);
        
        // put back our custom child signal handler
        sigchld = signal(SIGCHLD, SIG_IGN);
      }

      // if we're forking a child process for each connection...
      else
      {
        log("Forking child to handle connection...\n");

        int pid = fork();
        if (pid < 0)
        {
          log("Could not fork child process to handle request.\n");
          log_errno();
        }
        else if (pid > 0) // parent process
        {
          log("Child process forked...\n");
          close(connection_fd);
        }
        else // child process
        {
          // close child's copy of listener socket
          close(m_socket_fd);

          // restore original child handling,
          // so processes forked by child can be waited on
          signal(SIGCHLD, sigchld);

          // delegate to connection handler method
          log("Forked child delegating to handler method...\n");
          result = handle_connection(connection_fd);
          log("Handler method returned %i.\n", result);

          // close the connection
          close(connection_fd);

          // exit child process
          exit(0);
        }
      }
    }
  }

  log("Server exited.\n");

  return result;
}


/** Makes server a daemon process. */
void
Server::daemonize()
{
  // clear file creation mask
  umask(0);

  // ignore certain signals
  signal(SIGTTOU, SIG_IGN);
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTSTP, SIG_IGN);

  // fork child (parent immediately exits with "ok" 0 status)
  if (0 != fork()) exit(0); 

  // create a new session
  if (-1 == setsid()) exit(-1);

  // ignore logout signal
  signal(SIGHUP, SIG_IGN);

  // fork again so we're not associated with any terminals
  if (0 != fork()) exit(0); 

  // cd to root directory by default
  chdir("/");

  // close standard I/O file descriptors
  // (any log files, etc. created by parent are still open)
  fclose(stdin);
  fclose(stdout);
  fclose(stderr);

  // reopen standard I/O fds on /dev/null
  open("/dev/null", O_RDWR);
  dup(0);
  dup(0);
}


/** Invoked to handle each connection.
    Note: handler method should NOT close the file descriptor.
    The server will do this when the handler returns.
 */
int
Server::handle_connection(int fd)
{
  log("Warning: default handle_connection() invoked for connection from fd %i\n", fd);
  return 0;
}


// --- log methods ---

/** Logs message to log specified in config file. */
void
Server::log(const std::string& format, ...) const
{
  if (m_debug_log)
  {
    va_list varargs;
    va_start(varargs, format);
    vfprintf(m_debug_log, format.c_str(), varargs);
    va_end(varargs);
    fflush(m_debug_log);
  }
}

/** Logs message to specified stream, overriding config file. */
void
Server::log(FILE* stream, const std::string& format, ...) const
{
  if (stream != NULL)
  {
    va_list varargs;
    va_start(varargs, format);
    vfprintf(stream, format.c_str(), varargs);
    va_end(varargs);
    fflush(stream);
  }
}

/** Logs errno value and string to log specified in config file. */
void
Server::log_errno() const
{
  if (m_debug_log)
  {
    fprintf(m_debug_log, "Error (%i): %s\n", errno, strerror(errno));
  }
}

/** Logs errno value and string to specified stream, overriding config file. */
void
Server::log_errno(FILE* stream) const
{
  if (stream != NULL)
  {
    fprintf(stream, "Error (%i): %s\n", errno, strerror(errno));
  }
}
