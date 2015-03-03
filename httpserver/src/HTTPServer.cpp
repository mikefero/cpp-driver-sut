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
#include "HTTPServer.hpp"

#include "easylogging++.h"
#include "Utilities.hpp"

#define SERVER_POLLING_IN_MILLISECONDS 1000

//Initialize the static variables
bool HTTPServer::isRunning_ = false;
bool HTTPServer::isStopped_ = true;
HTTPRequestResponse (*HTTPServer::serviceRequestFunction_)(std::string, std::string) = NULL;

void* HTTPServer::handleStartHTTPServer(void *server) {
  //Ensure the server is valid
  if (server) {
    //Start the polling of the server
    isRunning_ = true;
    isStopped_ = false;
    mg_server *serverInstance = static_cast<struct mg_server*>(server);
    while(isRunning_) {
      mg_poll_server(serverInstance, SERVER_POLLING_IN_MILLISECONDS); 
    }

    //Clean up resources and indicate the server has stopped
    mg_destroy_server(&serverInstance);
    isStopped_ = true;
    serviceRequestFunction_ = NULL;
  }
}

int HTTPServer::handleRequest(struct mg_connection *connection, enum mg_event event) {
  switch (event) {
    case MG_AUTH:
      return MG_TRUE;
    case MG_REQUEST:
      return processRequest(connection);
    default:
      return MG_FALSE;
  }
}

int HTTPServer::processRequest(struct mg_connection *connection) {
  //Get the URI from the connection request
  std::string uri = TO_STRING(connection->uri);
  std::string requestMethod = TO_STRING(connection->request_method);
  HTTPRequestResponse response = { 500, std::string("Internal Server Error") };

  //Determine if the URI should be processed
  if (uri.compare(ROOT_URI) == 0 || uri.compare(CASSANDRA_URI) == 0) {
    //Handle the GET request URIs
    if (requestMethod.compare("GET") == 0) {
      //Service the request and display the results
      response = (*serviceRequestFunction_)(uri, "");
    }
  } else if (uri.compare(KEYSPACE_URI) == 0) {
    std::string operation;
    if (requestMethod.compare("POST") == 0) {
      operation = "create";
    } else if (requestMethod.compare("PUT") == 0) {
      operation = "use";
    } else if (requestMethod.compare("DELETE") == 0) {
      operation = "drop";
    }

    response = (*serviceRequestFunction_)(uri, operation);
  } else if (uri.compare(0, strlen(PREPARED_STATEMENTS_USERS_URI), PREPARED_STATEMENTS_USERS_URI) == 0 || uri.compare(0, strlen(SIMPLE_STATEMENTS_USERS_URI), SIMPLE_STATEMENTS_USERS_URI) == 0) {
    //Determine if select or insert users is to occur
    std::stringstream data;
    if (requestMethod.compare("GET") == 0) {
      data << "select";
    } else if (requestMethod.compare("POST") == 0) {
      data << "insert";
    }

    //Determine the id and number of users from the URI or data
    char id[1024] = { '\0' };
    char count[1024] = { '\0' };
    int userURIPosition = 1 + (uri.compare(0, strlen(PREPARED_STATEMENTS_USERS_URI), PREPARED_STATEMENTS_USERS_URI) == 0 ? strlen(PREPARED_STATEMENTS_USERS_URI) : strlen(SIMPLE_STATEMENTS_USERS_URI));
    if (uri.length() > userURIPosition) {
      std::stringstream userURIStream(uri.substr(userURIPosition));
      std::string token;
      int n = 0;
      while (std::getline(userURIStream, token, '/')) {
        if (++n == 1 ) {
          data << "&id=" << token;
        } else if (n == 2) {
          data << "&count=" << token;
        } else {
          LVERBOSE(1) << "URI Contains Additional Data: " << token << " will be ignored";
        }
      }
      LVERBOSE(1) << "Parsing ID and Number of Users from URI: " << data.str();
    } else {
      //Get the ID
      if (mg_get_var(connection, "id", id, sizeof(id)) >= 0) {
        data << "&id=" << id;
      }

      //Get the count
      if (mg_get_var(connection, "nb-users", count, sizeof(count)) >= 0) {
        data << "&count=" << count;
      }
      LVERBOSE(1) << "Parsing ID and Number of Users from Data Fields: " << data.str();
    }

    //Service the request and display the results
    response = (*serviceRequestFunction_)(uri, data.str());
  }

  //Invalid URI
  mg_send_status(connection, response.statusCode);
  mg_printf_data(connection, response.message.c_str(), 0);
  return MG_TRUE;
}

HTTPServer::HTTPServer(unsigned short port /* = DEFAULT_PORT */) : port_(port) {
}

HTTPServer::~HTTPServer() {
  //Stop the server
  stop();
}

void HTTPServer::start(HTTPRequestResponse (*serviceRequestFunction)(std::string uri, std::string data)) {
  //Determine if the server is already running
  if (!isRunning_) {
    //Create the server instance and start the server in its own thread
    serviceRequestFunction_ = serviceRequestFunction;
    struct mg_server *server = mg_create_server(NULL, handleRequest);
    mg_set_option(server, "listening_port", TO_CSTR(port_));
    mg_start_thread(handleStartHTTPServer, server);
  }
}

void HTTPServer::stop() {
  if (isRunning_) {
    //Stop the server and block until the server has stopped
    isRunning_ = false;
    while(!isStopped_) {}
  }
}

