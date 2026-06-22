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
// Server.h -- socket-based server
// Copyright (C) 2010. Tilera Corporation
// =============================================================================

// Multiple-inclusion guard.
#ifndef SERVER_H
#define SERVER_H

// C/C++ includes.
#include <arpa/inet.h>  // inet_addr
#include <string>       // std::string

// custom classes
#include "Pathname.h"   // Pathname


// -----------------------------------------------------------------------------
// constants
// -----------------------------------------------------------------------------

/** Default server port. */
#define DEFAULT_PORT 8080

/** Default .conf file path. */
#define DEFAULT_CONF_PATH "/etc/tile-thd/conf"


// -----------------------------------------------------------------------------
// Server
// -----------------------------------------------------------------------------

/** Socket-based server (base class). */
class Server
{

  // --- members ---
protected:
  /** Path to configuration file, if any. */
  Pathname m_config_file_path;

  /** Port number to bind to. */
  int m_port;

  /** Specific interface address, if any, to bind to. */
  std::string m_address;

  /** Whether to run server as daemon (background) process. */
  bool m_daemonize;

  /** Whether to fork child process for each connection. */
  bool m_fork;

  /** Path for pid file. */
  Pathname m_pid_file;

  /** Debug log stream. */
  FILE* m_debug_log;

  
  // --- internal members ---
private:
  /** Listen socket. */
  int m_socket_fd;


  // --- constructors/destructors ---
public:
  /** Constructor. */
  Server(const Pathname& config_file_path = "");

protected:
  /** Copy constructor. */
  Server(const Server& server);

  /** Assignment operator. */
  Server&
  operator==(const Server& server);

public:
  /** Destructor. */
  virtual ~Server();


  // --- init methods ---
protected:
  /** Initializes members to default values. */
  void
  initialize(const Pathname& config_file_path);

  /** Cleans up members. */
  void
  cleanup();


  // --- accessors ---
public:
  /** Sets configuration file path. */
  void
  set_config_file(const Pathname& config_file_path);

  /** Sets interface address to bind to. */
  void
  set_address(const std::string& address);

  /** Sets interface address to bind to. */
  void
  set_port(int port);

  /** Sets whether server runs as background daemon. */
  void
  set_daemonize(bool daemonize);

  /** Sets whether server forks a child for each request. */
  void
  set_fork(bool fork);

  /** Sets PID file path.
      Can also be the empty string, to disable the PID file.
   */
  void
  set_pid_file(const Pathname& path);

  /** Sets debug log file path.
      Can also be "stdout" or "stderr" for the standard IO streams,
      or the empty string, to disable logging.
   */
  void
  set_debug_log(const Pathname& path);

  /** Sets debug log stream.
      Can be stdout, stderr, or a custom stream. */
  void
  set_debug_log(FILE* stream);


  // --- config file methods ---
public:
  /** Loads configuration file. */
  virtual void
  load_config_file(const Pathname& config_file_path = "");

protected:
  /** Handles configuration parameter.
      Returns true if parameter is recognized and handled, false if not.
      Intended to be overridden by subtypes; overriding method
      can call superclass method to handle generic server properties.
    */
  virtual bool
  handle_config_parameter(const std::string& name, const std::string& value);


  // --- methods ---
public:
  /** Runs server. */
  int
  run();

protected:
  /** Makes server a daemon process. */
  void
  daemonize();

  /** Invoked to handle each connection.
      Note: handler method should NOT close the file descriptor.
      The server will do this when the handler returns.
   */
  virtual int
  handle_connection(int fd);


  // --- logging methods ---
public:

  /** Logs message to log specified in config file. */
  void
  log(const std::string& format, ...) const;

  /** Logs message to specified stream, overriding config file. */
  void
  log(FILE* stream, const std::string& format, ...) const;

  /** Logs errno value and string to log specified in config file. */
  void
  log_errno() const;

  /** Logs errno value and string to specified stream, overriding config file. */
  void
  log_errno(FILE* stream) const;

};

// Multiple-inclusion guard.
#endif
