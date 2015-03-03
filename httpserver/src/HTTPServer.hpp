/**
 * Copyright (c) 2015 DataStax
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef _HTTP_SERVER_HPP
#define _HTTP_SERVER_HPP
#include <string>

#include <mongoose.h>
#include <uv.h>

//Create macros for the http server defaults
#define DEFAULT_PORT 8080

//Create macros for URI processing
#define CASSANDRA_URI "/cassandra"
#define KEYSPACE_URI "/keyspace"
#define SIMPLE_STATEMENTS_USERS_URI "/simple-statements/users"
#define PREPARED_STATEMENTS_USERS_URI "/prepared-statements/users"
#define ROOT_URI "/"

//Create macros for HTTP status codes
#define HTTP_STATUS_CODE_OK 200
#define HTTP_STATUS_CODE_CREATED 201
#define HTTP_STATUS_CODE_NO_CONTENT 204
#define HTTP_STATUS_CODE_NOT_MODIFIED 304
#define HTTP_STATUS_CODE_NOT_FOUND 404
#define HTTP_STATUS_CODE_CONFLICT 409
#define HTTP_STATUS_CODE_INTERNAL_SERVER_ERROR 500

/**
 * HTTP request reposnse
 */
struct HTTPRequestResponse {
  /**
   * Status code for HTTP request
   */
  unsigned int statusCode;
  /**
   * Message response for HTTP request
   *
   * NOTE: This may also be a response for a particular request
   */
  std::string message;
  /**
   * Duration for the HTTP request
   */
  double duration;
  /**
   * Human readable duration (days/hours/minutes/...etc) for the HTTP request
   */
  std::string formattedDuration;
};

/**
 * HTTP server instance to listen to requests for the Software Under Test (SUT)
 */
class HTTPServer {
private:
  /**
   * HTTP listen port
   */
  unsigned short port_;
  /**
   * Flag to determine if the HTTP server is running or not
   */
  static bool isRunning_;
  /**
   * Flag to determine if the HTTP server has stopped or not
   */
  static bool isStopped_;
  /**
   * Function pointer to execute for servicing requests
   */
  static HTTPRequestResponse (*serviceRequestFunction_)(std::string uri, std::string data);

  /**
   * Handler for starting the HTTP server in its own thread
   *
   * @param server Server instance
   */
  static void* handleStartHTTPServer(void *server);
  /**
   * Handler for incoming HTTP request
   *
   * @param connection Connection information for incoming request
   * @param event Request event
   * @return Result for the request
   */
  static int handleRequest(struct mg_connection *connection, enum mg_event event);
  /**
   * Process the incoming request based on the URI
   *
   * @param connection Connection information for incoming request
   * @return Result for the request
   */
  static int processRequest(struct mg_connection *connection);

public:
  /**
   * Constructor
   *
   * @param port Port to listen on (default: DEFAULT_PORT)
   */
  HTTPServer(unsigned short port = DEFAULT_PORT);
  /**
   * Destructor
   */
  ~HTTPServer();
  /**
   * Start the HTTP server
   *
   * @param serviceRequestFunction Callback function for servicing requests
   */
  void start(HTTPRequestResponse (*serviceRequestFunction)(std::string uri, std::string data));
  /**
   * Stop the HTTP server
   */
  void stop();
};
#endif //_HTTP_SERVER_HPP
