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
#include <algorithm>
#include <limits>
#include <math.h>
#ifndef _WIN32
#	include <signal.h>
#endif
#include <stdio.h>
#include <string>

#include <uv.h>

#include <cassandra.h>

#include "CQLConnection.hpp"
#include "HTTPServer.hpp"
#include "Timer.hpp"
#include "Utilities.hpp"

//Initialize the logger
#include "easylogging++.h"
_INITIALIZE_EASYLOGGINGPP

//Create macros for time related calculations
#define MILLISECONDS_PER_MICROSECOND 1000
#define MILLISECONDS_PER_SECOND 1000
#define SECONDS_PER_MINUTE 60
#define MINUTES_PER_HOUR 60
#define HOURS_PER_DAY 24

//Create macros for the http server defaults
#define DEFAULT_DRIVER_LOGGING false
#define DEFAULT_DRIVER_LOGGING_LEVEL CASS_LOG_DEBUG
#define DEFAULT_DRIVER_LOGGING_LEVEL_DISPLAY_NAME "Debug"
#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_OVERWRITE_LOGS true
#define DEFAULT_LISTEN_PORT 8080
#define DEFAULT_SSL false

//Create macros for reusable constants
#define LOG_DIRECTORY "logs"
#define SELECT_NOW_CQL_QUERY "SELECT dateOf(NOW()) FROM system.local"
#define CREATE_KEYSPACE_CQL_QUERY "CREATE KEYSPACE videodb WITH REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : 3 };"
#define CREATE_USERS_TABLE_CQL_QUERY "CREATE TABLE videodb.users (username varchar, firstname varchar, lastname varchar, email list<varchar>, password varchar, created_date timestamp, PRIMARY KEY (username));"
#define CREATE_VIDEOS_TABLE_CQL_QUERY "CREATE TABLE videodb.videos (videoid uuid, videoname varchar, username varchar, description varchar, location map<varchar,varchar>, tags set<varchar>, upload_date timestamp, PRIMARY KEY (videoid));"
#define CREATE_USERNAME_VIDEO_INDEX_TABLE_CQL_QUERY "CREATE TABLE videodb.username_video_index (username varchar, videoid uuid, upload_date timestamp, videoname varchar, PRIMARY KEY (username,upload_date,videoid)) WITH CLUSTERING ORDER BY (upload_date DESC);"
#define CREATE_VIDEO_RATING_TABLE_CQL_QUERY "CREATE TABLE videodb.video_rating (videoid uuid, rating_counter counter, rating_total counter, PRIMARY KEY (videoid));"
#define CREATE_TAG_INDEX_TABLE_CQL_QUERY "CREATE TABLE videodb.tag_index (tag varchar, videoid uuid, tag_ts timestamp, PRIMARY KEY (tag, videoid));"
#define CREATE_COMMENTS_BY_VIDEO_TABLE_CQL_QUERY "CREATE TABLE videodb.comments_by_video (videoid uuid, username varchar, comment_ts timeuuid, comment varchar, PRIMARY KEY (videoid,comment_ts,username)) WITH CLUSTERING ORDER BY (comment_ts DESC, username ASC);"
#define CREATE_COMMENTS_BY_USER_TABLE_CQL_QUERY "CREATE TABLE videodb.comments_by_user (username varchar, videoid uuid, comment_ts timeuuid, comment varchar, PRIMARY KEY (username,comment_ts,videoid)) WITH CLUSTERING ORDER BY (comment_ts DESC, videoid ASC);"
#define CREATE_VIDEO_EVENT_TABLE_CQL_QUERY "CREATE TABLE videodb.video_event ( videoid uuid, username varchar, event varchar, event_timestamp timeuuid, video_timestamp bigint, PRIMARY KEY ((videoid,username),event_timestamp,event)) WITH CLUSTERING ORDER BY (event_timestamp DESC,event ASC);"
#define USE_KEYSPACE_CQL_QUERY "USE videodb"
#define DROP_KEYSPACE_CQL_QUERY "DROP KEYSPACE videodb"
#define SELECT_USER_CQL_QUERY "SELECT username, firstname, lastname, password, email, created_date FROM videodb.users WHERE username = '%s'"
#define INSERT_USER_CQL_QUERY "INSERT INTO videodb.users (username, firstname, lastname, email, password, created_date) VALUES ('%s', '%s', '%s', ['%s'], '%s', dateOf(NOW()))"

#ifdef _WIN32
#	define tzset _tzset
# define MICROSECOND_SYMBOL "\xE6"
#else
# define MICROSECOND_SYMBOL "\xC2\xB5"
#endif

/**
 * HTTP server configuration structure; holds command line argument overrides
 * or default assignments
 */
struct HTTPServerConfiguration {
  /**
   * Driver logging level
   */
  CassLogLevel driverLoggingLevel;
  /**
   * Display name for the driver logging level
   */
  std::string driverLoggingLevelDisplayName;
  /**
   * Node to use as initial contact point
   */
  const char* host;
  /**
   * Port to listen for HTTP requests on
   */
  unsigned short listeningPort;
  /**
   * Flag to enable/disable driver logging
   */
  bool isDriverLogging;
  /**
   * Flag to enable/disable overwrite of log files
   */
  bool isOverwriteLogs;
  /**
   * Flag to enable/disable SSL
   */
  bool isSSL;
};

/**
 * Flag to determine if the benchmark test is still running
 */
bool IS_RUNNING = false;
/**
 * Counter to keep track of the number of requests processed
 */
unsigned long long NUMBER_OF_REQUESTS = 0;
/**
 * Counter to keep track of the number of failed requests
 */
unsigned long long NUMBER_OF_FAILED_REQUESTS = 0;
/**
 * Driver connection to Cassandra Cluster
 */
CQLConnection *DRIVER_CONNECTION = NULL;
/**
 * C++ driver prepared insert statement
 */
CassPrepared *PREPARED_INSERT_STATEMENT = NULL;
/**
 * C++ driver prepared select statement
 */
CassPrepared *PREPARED_SELECT_STATEMENT = NULL;
/**
 * Timer to help determine elapsed time
 */
Timer ELAPSED_TIMER;

#ifndef _WIN32
/**
 * Signal handler for proper application shutdown
 *
 * @param signal The signal to handle
 */
void signalHandler(int signal) {
  //Determine which signal was caught
  std::string signalName;
  switch (signal) {
    case SIGTERM:
      signalName = "SIGTERM";
      break;
    case SIGKILL:
      signalName = "SIGKILL";
      break;
    case SIGINT:
      signalName = "SIGINT";
      break;
    default:
      //Short circuit (ignore signal)
      return;
  }

  //Determine which message to present to the user
  if (signal == SIGINT) {
    LOG(INFO) << signalName << " was detected ... Shutting down HTTP server";
  } else {
    LOG(FATAL) << signalName << " was detected ... Attempting to gracefully shutdown";
  }

  //Stop the running application thread
  IS_RUNNING = false;
}
#endif

/**
 * Calculate the duration time (in days) for a given elapsed time
 *
 * @param elapsedTime Elapsed time (in microseconds)
 * @param granularity Granularity for seconds display
 *                    (default: TIME_TYPE_SECONDS)
 * @return A string representation of the elapsed time (in days)
 */
std::string calculateDurationTime(double elapsedTime, const TimeType granularity = TIME_TYPE_SECONDS) {
  unsigned int microseconds = fmod(elapsedTime, MILLISECONDS_PER_MICROSECOND);
  unsigned int stepDownCalculation = static_cast<unsigned int>(elapsedTime / MILLISECONDS_PER_MICROSECOND);
  unsigned int milliseconds = stepDownCalculation % MILLISECONDS_PER_SECOND;
  stepDownCalculation /= MILLISECONDS_PER_SECOND;
  unsigned int seconds = stepDownCalculation % SECONDS_PER_MINUTE;
  stepDownCalculation /= SECONDS_PER_MINUTE;
  unsigned int minutes = stepDownCalculation % MINUTES_PER_HOUR;
  stepDownCalculation /= MINUTES_PER_HOUR;
  unsigned int hours = stepDownCalculation % HOURS_PER_DAY;
  unsigned int days = stepDownCalculation / HOURS_PER_DAY;

  //Create the human readable duration time appropriately
  std::stringstream durationTime;
  //Day
  if (days > 0) {
    durationTime << days << "d ";
  }
  //Hour
  if (days > 0 || hours > 0) {
    durationTime << hours << "h ";
  }
  //Minutes
  if ((days > 0 || hours > 0) || minutes > 0) {
    durationTime << minutes << "m ";
  }
  //Seconds
  if ((days > 0 || hours > 0 || minutes > 0) || seconds > 0) {
    durationTime << seconds << "s ";
  }
  //Milliseconds
  if (granularity != TIME_TYPE_SECONDS && ((days > 0 || hours > 0 || minutes > 0 || seconds > 0) || milliseconds > 0)) {
    durationTime << milliseconds << "ms ";
  }
  //Microseconds
  if (granularity == TIME_TYPE_MICROSECONDS) {
    durationTime << microseconds << MICROSECOND_SYMBOL << "s";
  }

  return durationTime.str();
}

/**
 * Select/Insert n users from/into the Cassandra database
 *
 * @param isSelect True if select should be performed; false otherwise
 * @param isPrepared True if using prepared statement; false otherwise
 * @param id ID to select/insert
 * @param count Number of select/insert operations to perform
 * @return Response to the operation
 */
std::string selectInsertUsers(bool isSelect, bool isPrepared, int id, int count) {
  std::stringstream response;

  //Ensure the driver connection was established
  if (DRIVER_CONNECTION) {
    //Select or insert n users
    int n = id;
    count = (count == 0) ? 1 : count;
    for (n; n < (id + count); ++n) {
      if (isSelect) {
        const CassResult *resultSet = NULL;
        if (isPrepared) {
          //Determine if the prepared select statement should be prepared
          if (!PREPARED_SELECT_STATEMENT) {
            //Re-format the statement to be a prepared statement
            char selectBuffer[1024] = { '\0' };
            snprintf(selectBuffer, 1023, SELECT_USER_CQL_QUERY, "?");
            PREPARED_SELECT_STATEMENT = DRIVER_CONNECTION->createPreparedStatement(selectBuffer);
          }

          //Bind and execute the statement
          CassStatement* statement = cass_prepared_bind(PREPARED_SELECT_STATEMENT);
          CassString userInformation = cass_string_init(TO_CSTR(n));
          cass_statement_bind_string(statement, 0, userInformation);
          resultSet = DRIVER_CONNECTION->executeStatementUntilPass(statement, true);
        } else {
          resultSet = DRIVER_CONNECTION->executeStatementUntilPass(DRIVER_CONNECTION->createStatement(SELECT_USER_CQL_QUERY, 1, TO_CSTR(n)), true);
        }
        const CassRow *firstRow = cass_result_first_row(resultSet);
        if (firstRow) {
          //Get the username from the row
          CassString username;
          cass_value_get_string(cass_row_get_column_by_name(firstRow, "username"), &username);

          //Update the response
          if ((n - id) != 0) {
            response << ",";
          }
          response << std::string(username.data, username.length);
        }

        //Free the resources
        cass_result_free(resultSet);
      } else {
        if (isPrepared) {
          //Determine if the prepared insert statement should be prepared
          if (!PREPARED_INSERT_STATEMENT) {
            //Re-format the statement to be a prepared statement
            char insertBuffer[1024] = { '\0' };
            snprintf(insertBuffer, 1023, INSERT_USER_CQL_QUERY, "?", "?", "?", "?", "?");
            PREPARED_INSERT_STATEMENT = DRIVER_CONNECTION->createPreparedStatement(insertBuffer);
          }
          
          //Bind and execute the statement
          CassStatement* statement = cass_prepared_bind(PREPARED_INSERT_STATEMENT);
          CassString userInformation = cass_string_init(TO_CSTR(n));
          cass_statement_bind_string(statement, 0, userInformation);
          cass_statement_bind_string(statement, 1, userInformation);
          cass_statement_bind_string(statement, 2, userInformation);
          cass_statement_bind_string(statement, 3, userInformation);
          cass_statement_bind_string(statement, 4, userInformation);
          CassError resultCode = DRIVER_CONNECTION->executeStatement(statement);

        } else {
          CassError resultCode = DRIVER_CONNECTION->executeStatement(DRIVER_CONNECTION->createStatement(INSERT_USER_CQL_QUERY, 5, TO_CSTR(n), TO_CSTR(n), TO_CSTR(n), TO_CSTR(n), TO_CSTR(n)));
        }
      }
    }

    //Update and return the response
    if (!isSelect) {
      response << "Insert Complete: " << (n - 1) << " user(s) inserted [" << id << " - " << count << "]";
    }
  }

  //Return the resonse for the operation
  return response.str();
}

HTTPRequestResponse serviceHTTPRequest(std::string uri, std::string data) {
  //Flag for checking if a request is serviceable 
  bool isRequestServiceable = true;
  unsigned int requestStatusCode = HTTP_STATUS_CODE_OK;
  std::stringstream requestResultMessage;
    
  //Start a timer for calculating the response time
  Timer responseTimer;
  responseTimer.start();

  if (uri.compare(ROOT_URI) == 0) {
    requestResultMessage << "Hello World!";
  } else if (uri.compare(CASSANDRA_URI) == 0) {
    //Ensure the driver connection was established
    if (DRIVER_CONNECTION) {
      //Get the Cassandra version
      CassVersion version = DRIVER_CONNECTION->getCassandraVersion();

     //Execute the query and get the time from the result (human readable)
     const CassResult *resultSet = DRIVER_CONNECTION->executeStatementUntilPass(DRIVER_CONNECTION->createStatement(SELECT_NOW_CQL_QUERY), true);
     cass_int64_t timestamp;
     cass_value_get_int64(cass_row_get_column(cass_result_first_row(resultSet), 0), &timestamp);
     timestamp /= MILLISECONDS_IN_SECONDS;
     cass_result_free(resultSet);
     requestResultMessage << "Cassandra Version: " << version.major << "." << version.minor << "." << version.patch << "-" << version.extra << "\n"
                   << "             Time: " << ctime(&timestamp);
    } else {
      requestStatusCode = HTTP_STATUS_CODE_INTERNAL_SERVER_ERROR;
    }
  } else if (uri.compare(KEYSPACE_URI) == 0) {
    if (data.compare("create") == 0) {
      //Ensure the driver connection was established
      if (DRIVER_CONNECTION) {
        CassError resultCode = DRIVER_CONNECTION->executeQuery(CREATE_KEYSPACE_CQL_QUERY);
        if (resultCode == CASS_OK) {
          resultCode = DRIVER_CONNECTION->executeQuery(CREATE_USERS_TABLE_CQL_QUERY);
          if (resultCode == CASS_OK) {
            resultCode = DRIVER_CONNECTION->executeQuery(CREATE_VIDEOS_TABLE_CQL_QUERY);
            if (resultCode == CASS_OK) {
              resultCode = DRIVER_CONNECTION->executeQuery(CREATE_USERNAME_VIDEO_INDEX_TABLE_CQL_QUERY);
              if (resultCode == CASS_OK) {
                resultCode = DRIVER_CONNECTION->executeQuery(CREATE_VIDEO_RATING_TABLE_CQL_QUERY);
                if (resultCode == CASS_OK) {
                  resultCode = DRIVER_CONNECTION->executeQuery(CREATE_TAG_INDEX_TABLE_CQL_QUERY);
                  if (resultCode == CASS_OK) {
                    resultCode = DRIVER_CONNECTION->executeQuery(CREATE_COMMENTS_BY_VIDEO_TABLE_CQL_QUERY);
                    if (resultCode == CASS_OK) {
                      resultCode = DRIVER_CONNECTION->executeQuery(CREATE_COMMENTS_BY_USER_TABLE_CQL_QUERY);
                      if (resultCode == CASS_OK) {
                        resultCode = DRIVER_CONNECTION->executeQuery(CREATE_VIDEO_EVENT_TABLE_CQL_QUERY);
                      }
                    }
                  }
                }
              }
            }
          }

          //Determine if an error occurred along the way
          if (resultCode == CASS_OK) {
            //Indicate the keyspace and tables were created
            requestStatusCode = HTTP_STATUS_CODE_CREATED;
          } else {
            //Indicate that an error occurred while creating the table
            requestStatusCode = HTTP_STATUS_CODE_INTERNAL_SERVER_ERROR;
          }
        } else {
          if (resultCode == CASS_ERROR_SERVER_ALREADY_EXISTS) {
            //Indicate that the table already existed
            requestStatusCode = HTTP_STATUS_CODE_NOT_MODIFIED;
          }
        }
      }
    } else if (data.compare("use") == 0) {
      //Ensure the driver connection was established
      if (DRIVER_CONNECTION) {
        CassError resultCode = DRIVER_CONNECTION->executeQuery(USE_KEYSPACE_CQL_QUERY);
        if (resultCode == CASS_OK) {
          //Indicate the keyspace was switched (e.g. used)
          requestStatusCode = HTTP_STATUS_CODE_NO_CONTENT;
        } else {
          if (resultCode == CASS_ERROR_SERVER_INVALID_QUERY) {
            requestStatusCode = HTTP_STATUS_CODE_NOT_MODIFIED;
          } else {
            requestStatusCode = HTTP_STATUS_CODE_INTERNAL_SERVER_ERROR;
          }
        }
      }
    } else if (data.compare("drop") == 0) {
      //Ensure the driver connection was established
      if (DRIVER_CONNECTION) {
        CassError resultCode = DRIVER_CONNECTION->executeQuery(DROP_KEYSPACE_CQL_QUERY);
        if (resultCode == CASS_OK) {
          requestStatusCode = HTTP_STATUS_CODE_NO_CONTENT;
        } else if (resultCode == CASS_ERROR_SERVER_CONFIG_ERROR) {
          requestStatusCode = HTTP_STATUS_CODE_NOT_MODIFIED;
        } else {
          requestStatusCode = HTTP_STATUS_CODE_INTERNAL_SERVER_ERROR;
        }
      }
    }
  } else if (uri.compare(0, strlen(PREPARED_STATEMENTS_USERS_URI), PREPARED_STATEMENTS_USERS_URI) == 0 || uri.compare(0, strlen(SIMPLE_STATEMENTS_USERS_URI), SIMPLE_STATEMENTS_USERS_URI) == 0) {
    bool isSelect = false;
    int id = 0;
    int count = 0;

    std::stringstream dataStream(data);
    std::string token;
    bool isParsingError = false;
    while (std::getline(dataStream, token, '&')) {
      if (token.compare("select") == 0) {
        isSelect = true;
      } else {
        //Parse the key/value pair
        int position = token.find("=");
        std::string key = token.substr(0, position);
        std::stringstream value(token.substr(position + 1));

        //Determine if the ID or count is to be assigned
        if (key.compare("id") == 0) {
          if ((value >> id).fail()) {
            isParsingError = true;
            LOG(WARNING) << "Failed to Parse ID Value: " << value << " [" << uri << "]";
            break;
          }
        } else if (key.compare("count") == 0) {
          if (!value.str().empty()) {
            if ((value >> count).fail()) {
              LOG(WARNING) << "Failed to Parse Count/Number of IDs: " << value << " [" << uri << "]";
              break;
            }
          }
        }
      }
    }

    //Select/Insert the users (iff no parsing error occurred)
    if (!isParsingError) {
      bool isPrepared = (uri.compare(0, strlen(PREPARED_STATEMENTS_USERS_URI), PREPARED_STATEMENTS_USERS_URI) == 0) ? true : false;
      requestResultMessage << selectInsertUsers(isSelect, isPrepared, id, count);
    } else {
      requestResultMessage << "Error Parsing User Data: " << data;
    }
  } else {
    isRequestServiceable = false;
    requestResultMessage << "Invalid Request: URI " << uri << " was not serviced";
  }

  //Print status message to show progress while incrementing request counter
  if (isRequestServiceable) {
    if (++NUMBER_OF_REQUESTS % 100l == 0l && NUMBER_OF_REQUESTS != 0) {
      LOG(INFO) << "Processed: " << NUMBER_OF_REQUESTS << " requests in " << calculateDurationTime(ELAPSED_TIMER.getElapsedTime(), TIME_TYPE_SECONDS);
    }
  } else {
    //Increment the number of failed requests
    ++NUMBER_OF_FAILED_REQUESTS;
  }

  //Return the request result
   responseTimer.stop();
   requestResultMessage << "\n" << "Processed Request in: " << calculateDurationTime(responseTimer.getElapsedTime(), TIME_TYPE_MICROSECONDS) << " [" << static_cast<unsigned int>(responseTimer.getElapsedTime()) << "]\n";
   HTTPRequestResponse response = { requestStatusCode, requestResultMessage.str() };
  return response;
}

/**
 * Start the HTTP server
 *
 * @param arg HTTP server configuration
 */
void startHTTPServer(void *arg) {
  //Convert the configuration to a usable HTTP server configuration
  HTTPServerConfiguration *configuration = static_cast<HTTPServerConfiguration*>(arg);

  //Display the HTTP server benchmarking test settings
  LOG(INFO) << "Initializing HTTP Server";
  LOG(INFO) << "Cassandra Host:        " << configuration->host;
  LOG(INFO) << "Driver Logging:        " << (configuration->isDriverLogging ? "True" : "False");
  LOG(INFO) << "Driver Logging Level:  " << configuration->driverLoggingLevelDisplayName;
  LOG(INFO) << "Overwrite Log Files:   " << (configuration->isOverwriteLogs ? "True" : "False");
  LOG(INFO) << "Server Listening Port: " << configuration->listeningPort;
  LOG(INFO) << "SSL:                   " << (configuration->isSSL ? "True" : "False");

  //Create the cpp-driver connection
  try {
    DRIVER_CONNECTION = new CQLConnection(configuration->host, configuration->isSSL, configuration->isOverwriteLogs, configuration->driverLoggingLevel);
    CassVersion version = DRIVER_CONNECTION->getCassandraVersion();
    LOG(INFO) << "Cassandra Version:     " << version.major << "." << version.minor << "." << version.patch << "-" << version.extra;
  } catch (CQLConnectionException &cqlce) {
    DRIVER_CONNECTION = NULL;
    LOG(FATAL) << cqlce.what() << "\n" << Utilities::getStackTrace();
  }

  //Start the timer to calculate elapsed time
  ELAPSED_TIMER.start();

  //Start the HTTP server benchmarking
  IS_RUNNING = true;
  LOG(INFO) << "Starting HTTP Server Request Benchmarking";
  HTTPServer *httpServer = new HTTPServer(configuration->listeningPort);
  httpServer->start(&serviceHTTPRequest);
  while (IS_RUNNING) {
    LVERBOSE(3) << "Sleeping for One Second";
    Utilities::sleep(ONE_SECOND_SLEEP);
    LVERBOSE(3) << "Finished Sleeping";
  }

  //Terminate the HTTP server
  delete httpServer;

  //Terminate the connection to the Cassandra cluster
  cass_prepared_free(PREPARED_INSERT_STATEMENT);
  cass_prepared_free(PREPARED_SELECT_STATEMENT);
  delete DRIVER_CONNECTION;
  DRIVER_CONNECTION = NULL;

  //Stop the timer
  ELAPSED_TIMER.stop();

  //Indicate the benchmark is completed
  LOG(INFO) << "Executed: " << NUMBER_OF_REQUESTS << " requests";
  LOG(INFO) << "Time:     " << calculateDurationTime(ELAPSED_TIMER.getElapsedTime(), TIME_TYPE_SECONDS);
  LOG(INFO) << "HTTP Server Request Benchmarking Completed";
}

/**
 * Print the usage of the http server benchmarking application command line
 * arguments
 */
void printUsage() {
  printf("\nUsage: cpp-driver-http-server [OPTION...]\n\n");
  printf("\t%-20s%s%s\n", "--append_logs", "disable overwriting of log files (default ", DEFAULT_OVERWRITE_LOGS ? "enabled)" : "disabled)");
  printf("\t%-20s%s%s%s\n", "--driver_level", "assign driver logging level (default ", DEFAULT_DRIVER_LOGGING_LEVEL_DISPLAY_NAME, ")");
  printf("\t%-20s[CRITICAL | ERROR | WARN | INFO | DEBUG | TRACE]\n", "");
  printf("\t%-20s%s%s\n", "--driver_logging", "enable driver logging (default ", DEFAULT_DRIVER_LOGGING ? "enabled)" : "disabled)");
  printf("\t%-20s%s\n", "-h, --host", "node to use as initial contact point");
  printf("\t%-20s(default %s)\n", "", DEFAULT_HOST);
  printf("\t%-20s%s\n", "-l, --listen_port", "port to use for listening to HTTP requests");
  printf("\t%-20s(default %s)\n", "", TO_CSTR(DEFAULT_LISTEN_PORT));
  printf("\t%-20s%s%s\n", "--ssl", "enable SSL (default ", DEFAULT_SSL ? "enabled)" : "disabled)");
  printf("\t%-20s%s\n", "-v, --verbose", "enable verbose log output (level = 9)");
  printf("\t%-20s%s\n", "--v=[0-9]", "enable verbose log output at a level");
  printf("\n\t%-20s%s\n", "--help", "give this help list");
}

/**
 * Configure the logger
 *
 * NOTE: The logger can only be configured once
 *
 * @param logBasename The basename of the log file
 * @param isOverwriteLog True if driver log file should be overwritten;
 *                       false otherwise
 */
void configureLogger(std::string logBasename, bool isOverwriteLog) {
  //Create the relative path log filename
  std::string relativePathLogFilename(std::string(LOG_DIRECTORY) + "/" + logBasename + ".log");

  //Determine if the log file should be overwritten
  if (isOverwriteLog) {
    std::fstream overwriteLogStream(relativePathLogFilename.c_str(), std::fstream::out | std::fstream::trunc);
    overwriteLogStream.close();
  }

  //Create the configuration based off the defaults
  easyloggingpp::Configurations configuration;
  configuration.setToDefault();

  /*
   * Update the log out format
   *
   * Normal log messages
   * MM/DD/YYYY HH:MM:SS.mmm <log_level>: <log_message>
   *
   * Error/Fatal/Warning log messages
   * MM/DD/YYYY HH:MM:SS.mmm <log_level>: [<function_name>] <log_message>
   */
  configuration.setAll(easyloggingpp::ConfigurationType::Format, "%datetime %level: %log");
  configuration.set(easyloggingpp::Level::Error, easyloggingpp::ConfigurationType::Format, "%datetime %level: [%func] %log");
  configuration.set(easyloggingpp::Level::Fatal, easyloggingpp::ConfigurationType::Format, "%datetime %level: [%func] %log");
  configuration.set(easyloggingpp::Level::Warning, easyloggingpp::ConfigurationType::Format, "%datetime %level: [%func] %log");

  //Indicate the log file to use for all logging levels and enable it
  configuration.setAll(easyloggingpp::ConfigurationType::Filename, relativePathLogFilename);
  configuration.setAll(easyloggingpp::ConfigurationType::ToFile, "true");

  //Disable all but info, warning, error and fatal type messages for console
  configuration.setAll(easyloggingpp::ConfigurationType::ToStandardOutput, "false");
  configuration.set(easyloggingpp::Level::Info, easyloggingpp::ConfigurationType::ToStandardOutput, "true");
  configuration.set(easyloggingpp::Level::Warning, easyloggingpp::ConfigurationType::ToStandardOutput, "true");
  configuration.set(easyloggingpp::Level::Error, easyloggingpp::ConfigurationType::ToStandardOutput, "true");
  configuration.set(easyloggingpp::Level::Fatal, easyloggingpp::ConfigurationType::ToStandardOutput, "true");

  //Update the configuration for all loggers
  easyloggingpp::Loggers::reconfigureAllLoggers(configuration);
}

int main(int argc, char *argv[]) {
#ifndef _WIN32
  //Create the action handler and signals to catch for the application
  static struct sigaction signalAction;
  sigemptyset(&signalAction.sa_mask);
  signalAction.sa_flags = 0;
  signalAction.sa_handler = SIG_IGN;
  sigaction(SIGHUP, &signalAction, NULL);
  signalAction.sa_handler = signalHandler;
  sigaction(SIGINT, &signalAction, NULL);
  sigaction(SIGTERM, &signalAction, NULL);
  sigaction(SIGKILL, &signalAction, NULL);
#endif

  //Initialize the timezone
  tzset();

  //Assign the defaults 
  HTTPServerConfiguration configuration;
  configuration.driverLoggingLevel = CASS_LOG_DISABLED;
  configuration.driverLoggingLevelDisplayName = "Disabled";
  configuration.host = DEFAULT_HOST;
  configuration.listeningPort = DEFAULT_LISTEN_PORT;
  configuration.isDriverLogging = DEFAULT_DRIVER_LOGGING;
  configuration.isOverwriteLogs = DEFAULT_OVERWRITE_LOGS;
  configuration.isSSL = DEFAULT_SSL;

  //TODO: Look into crash with 64-bit windows EasyLogging++ Mutex if immediate exit
  //TODO: Convert argument to string and ignore case during comparison of arguments
  /**
   * Parse the command line arguments
   */
  for (int i = 1; i < argc; i++) {
    if ((strcmp("--append_logs", argv[i]) == 0)) {
        configuration.isOverwriteLogs = false;
    } else if ((strcmp("--driver_level", argv[i]) == 0)) {
      //Make sure there are arguments still available
      if ((i + 1) < argc) {
        //Convert the argument to upper case
        std::string argumentValue(argv[++i]);
        std::transform(argumentValue.begin(), argumentValue.end(), argumentValue.begin(), ::toupper);

        //Assign the proper driver logging level
        if (argumentValue.compare("CRITICAL") == 0) {
            configuration.driverLoggingLevel = CASS_LOG_CRITICAL;
            configuration.driverLoggingLevelDisplayName = "Critical";
            continue;
        } else if (argumentValue == "ERROR") {
            configuration.driverLoggingLevel = CASS_LOG_ERROR;
            configuration.driverLoggingLevelDisplayName = "Error";
            continue;
        } else if (argumentValue == "WARN") {
            configuration.driverLoggingLevel = CASS_LOG_WARN;
            configuration.driverLoggingLevelDisplayName = "Warning";
            continue;
        } else if (argumentValue == "INFO") {
            configuration.driverLoggingLevel = CASS_LOG_INFO;
            configuration.driverLoggingLevelDisplayName = "Info";
            continue;
        } else if (argumentValue == "DEBUG") {
            configuration.driverLoggingLevel = CASS_LOG_DEBUG;
            configuration.driverLoggingLevelDisplayName = "Debug";
            continue;
        } else if (argumentValue == "TRACE") {
            configuration.driverLoggingLevel = CASS_LOG_TRACE;
            configuration.driverLoggingLevelDisplayName = "Trace";
            continue;
        } else {
            printf("Invalid driver logging level: %s is not valid\n", argumentValue.c_str());
        }
      } else {
        printf("Invalid driver logging level: missing logging level\n");
      }

      //Invalid driver logging level or incorrect number of arguments
      printUsage();
      return 1;
    } else if ((strcmp("-h", argv[i]) == 0) || (strcmp("--host", argv[i]) == 0)) {
      //Make sure there are arguments still available
      if ((i + 1) < argc) {
        //Assign the host argument
        configuration.host = argv[++i];
        continue;
      }

      //Invalid host argument or incorrect number of arguments
      printf("Invalid host\n");
      printUsage();
      return 2;
    } else if ((strcmp("-l", argv[i]) == 0) || (strcmp("--listen_port", argv[i]) == 0)) {
      //Make sure there are arguments still available
      if ((i + 1) < argc) {
        //Convert the argument
        std::stringstream argumentValue(argv[++i]);
        if (!(argumentValue >> configuration.listeningPort).fail()) {
          continue;
        }
      }

      //Invalid listen port argument or incorrect number of arguments
      printf("Invalid listen port\n");
      printUsage();
      return 3;
    } else if ((strcmp("--driver_logging", argv[i]) == 0)) {
      configuration.isDriverLogging = true;

      //Determine if the driver logging level should be updated
      if (configuration.driverLoggingLevel == CASS_LOG_DISABLED) {
        configuration.driverLoggingLevel = DEFAULT_DRIVER_LOGGING_LEVEL;
        configuration.driverLoggingLevelDisplayName = DEFAULT_DRIVER_LOGGING_LEVEL_DISPLAY_NAME;
      }
    } else if (strcmp("--ssl", argv[i]) == 0) {
      //Enable SSL
      configuration.isSSL = true;
    } else if (strcmp("--help", argv[i]) == 0) {
      printUsage();
      return 0;
    }
  }

  //Configure the logger
  //TODO: Allow the logger to be configured via a configuration file
  _START_EASYLOGGINGPP(argc, argv);
  configureLogger("http_server", configuration.isOverwriteLogs);

  //Start the HTTP server
  uv_thread_t serverThread;
  uv_thread_create(&serverThread, startHTTPServer, &configuration);
  uv_thread_join(&serverThread);
  return 0;
}
