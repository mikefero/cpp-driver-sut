/**
 * Copyright (c) 2014 DataStax
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
#include "CQLConnection.hpp"
#include "Utilities.hpp"

#include "easylogging++.h"

#include <stdarg.h>
#include <stdio.h>
#ifndef _WIN32
#	include <unistd.h>
#else
#	define snprintf _snprintf_s
#	define vsnprintf vsnprintf_s
#endif

//Create macros for the CQL connection defaults
#define DEFAULT_DRIVER_LOGGING_FILENAME "logs/driver.log"
#define DEFAULT_NUMBER_OF_IO_WORKER_THREADS 4
#define DEFAULT_IO_QUEUE_SIZE 10000
#define DEFAULT_PENDING_REQUESTS_LOW_WATER_MARK 5000
#define DEFAULT_PENDING_REQUESTS_HIGH_WATER_MARK 10000
#define DEFAULT_CORE_CONNECTIONS_PER_HOST 1
#define DEFAULT_MAX_CONNECTIONS_PER_HOST 2

//Initialize constants
const char* CQLConnection::SELECT_CASSANDRA_RELEASE_VERSION = "SELECT release_version FROM system.local";

//Initialize the statics
uv_mutex_t CQLConnection::driverLogginglock_;

void CQLConnection::determineCassandraVersion() {
	//Create the statement for the version selection query
	CassString simpleQuery = cass_string_init(SELECT_CASSANDRA_RELEASE_VERSION);
	CassStatement* versionSelectStatement = cass_statement_new(simpleQuery, 0);

	//Execute the Cassandra version query and get the result
	const CassResult* resultSet = executeStatementUntilPass(versionSelectStatement, true);
	const CassRow* row = cass_result_first_row(resultSet);
	if (row) {
		//Convert the release_version value to a string
		const CassValue* value = cass_row_get_column_by_name(row, "release_version");
		CassString versionCassString;
		cass_value_get_string(value, &versionCassString);
		std::string versionString(versionCassString.data, versionCassString.length);

		//Convert the version string into the CassVersion structure
		sscanf(versionString.c_str(), "%hu.%hu.%hu-%s", &version_.major, &version_.minor, &version_.patch, version_.extra);
	}

	//Free the result set resources
	cass_result_free(resultSet);
}

void CQLConnection::handleDriverLogging(const CassLogMessage* message, void* data) {
	//Handle the log message (lock the logging mutex)
	uv_mutex_lock(&driverLogginglock_);
	std::fstream* loggingStream = static_cast<std::fstream*>(data);

	//Make sure the logging filestream is available
	if (loggingStream) {
		//Get the current date and time
		time_t currentTime;
		time(&currentTime);
		struct tm* dateTime = localtime(&currentTime); //TODO: Use message->time_ms (maybe?)

		//Create the date portion of the output message (DD/MM/YYYY)
		std::stringstream formattedDate;
		int month = (dateTime->tm_mon + 1);
		formattedDate << (dateTime->tm_mday < 10 ? "0" : "") << dateTime->tm_mday << "/";
		formattedDate << (month < 10 ? "0" : "") << month << "/";
		formattedDate <<  dateTime->tm_year + 1900;

		//Create the time portion of the output message (HH:MM:SS.XXX)
		std::stringstream formattedTime;
		formattedTime << (dateTime->tm_hour < 10 ? "0" : "") << dateTime->tm_hour << ":";
		formattedTime << (dateTime->tm_min < 10 ? "0" : "") << dateTime->tm_min << ":";
		formattedTime << (dateTime->tm_sec < 10 ? "0" : "") << dateTime->tm_sec << ".";
		formattedTime << currentTime % 1000; //TODO: Use message->time_ms (maybe?)

		//Output the log message (strip new line characters and flush output)
		std::string logMessage(message->message);
		logMessage.erase(std::remove(logMessage.begin(), logMessage.end(), '\n'), logMessage.end());
		*loggingStream << formattedDate.str() << " "
			<< formattedTime.str() << " "
			<< cass_log_level_string(message->severity) << ": "
			<< "[" << message->function << " " << message->file << ":" << message->line << "] "
			<< logMessage << std::endl;

		//Determine if the log message was successfully logged
		if (loggingStream->fail()) {
			LOG(ERROR) << "Unable to Write Driver Log Message: Closing stream";

			//Close the stream and free up the resources
			loggingStream->close();
			delete loggingStream;
			loggingStream = NULL;
		}
	}

	//Driver log message handled (unlock the logging mutex)
	uv_mutex_unlock(&driverLogginglock_);
}

CassPrepared* CQLConnection::createPreparedStatement(const char* query) {
  CassPrepared* prepared = NULL;

  //Create the query for the prepared statement
  CassString preparedQuery = cass_string_init(query);

  //Create the prepared statement
  CassFuture* preparedFuture = cass_session_prepare(session_, preparedQuery);
  cass_future_wait(preparedFuture);

  //Determine if the prepared statement was created
  CassError resultCode = cass_future_error_code(preparedFuture);
  if (resultCode != CASS_OK) {
    //Log the error message
    CassString message = cass_future_error_message(preparedFuture);
    LOG(ERROR) << std::string(message.data, message.length);
  } else {
    //Store the prepared statement for continual use
    prepared = const_cast<CassPrepared *>(cass_future_get_prepared(preparedFuture));
  }

  //Free up the resources and return the prepared statement
  cass_future_free(preparedFuture);
  return prepared;
}

CassStatement* CQLConnection::createStatement(const char* query, int count /* = 0 */, ...) {
  //Create a large buffer to hold the completed query string
  char queryBuffer[2048] = { '\0' };

  //Parse the formatting, apply arguments, and execute the statement
  va_list args;
  va_start(args, count);
  vsnprintf(queryBuffer, 2048, query, args);
  va_end(args);

  //Return the statement
  return cass_statement_new(cass_string_init(queryBuffer), 0);
}

CassError CQLConnection::executeQuery(const char* query, CassConsistency consistencyLevel /* = CASS_CONSISTENCY_ONE */, int count /* = 0 */, ...) {
  //Create a large buffer to hold the completed query string
  char queryBuffer[2048] = { '\0' };

  //Parse the formatting, apply arguments, and execute the statement
  va_list args;
  va_start(args, count);
  vsnprintf(queryBuffer, 2048, query, args);
  va_end(args);

  CassError resultCode = executeStatement(cass_statement_new(cass_string_init(queryBuffer), 0), true, consistencyLevel);

  //Return the result code
  return resultCode;
}

CassError CQLConnection::executeStatement(CassStatement* statement, bool isInvalidated /* = true */, CassConsistency consistencyLevel /* = CASS_CONSISTENCY_ONE */) {
  CassError resultCode = CASS_ERROR_LIB_BAD_PARAMS;

  //Ensure the statement can be executed
  if (statement) {
    //Execute the statement
    CassFuture* executeFuture = cass_session_execute(session_, statement);
    cass_future_wait(executeFuture);

    //Get the result code from the execution
    resultCode = cass_future_error_code(executeFuture);

    //Log the error message (if error occurred)
    if (resultCode != CASS_OK) {
      CassString message = cass_future_error_message(executeFuture);
      LOG(WARNING) << std::string(message.data, message.length) << "\n" << Utilities::getStackTrace();
    }

    //Free the resources
    cass_future_free(executeFuture);
  } else {
    LOG(WARNING) << "Unable to Execute Statement: Statement is NULL\n" << Utilities::getStackTrace();
  }

  //Free the statement resources (if requested) and return the result code
  if (isInvalidated) {
    cass_statement_free(statement);
  }
  return resultCode;
}

const CassResult* CQLConnection::executeStatementUntilPass(CassStatement* statement, bool isResultSetRequested /* = false */, bool isInvalidated /* = true */, CassConsistency consistencyLevel /* = CASS_CONSISTENCY_ONE */) {
  //Set statement consistency level
  cass_statement_set_consistency(statement, consistencyLevel);

  //Execute the statement
  CassFuture* executeFuture = NULL;
  while (true) {
    executeFuture = cass_session_execute(session_, statement);
    cass_future_wait(executeFuture);

    //Determine if the statement was executed
    CassError resultCode = cass_future_error_code(executeFuture);
    if (resultCode == CASS_OK) {
      //Successfully executed query; break out of the loop
      break;
    } else if (resultCode == CASS_ERROR_LIB_NO_HOSTS_AVAILABLE) {
      //Log a message and sleep for one second before trying again
      LOG(DEBUG) << "NHAE:sleep "; //TODO: Include query string?
      Utilities::sleep(ONE_SECOND_SLEEP);
    } else if (resultCode == CASS_ERROR_LIB_REQUEST_TIMED_OUT) {
      //Log a message and sleep for one second before trying again
      LOG(DEBUG) << "RTO:sleep "; //TODO: Include query string?
      Utilities::sleep(ONE_SECOND_SLEEP);
    } else {
      //Log the error message
      CassString message = cass_future_error_message(executeFuture);
      LOG(WARNING) << std::string(message.data, message.length) << "\n" << Utilities::getStackTrace();
    }
  }

  //Get the result set and make sure its not empty
  const CassResult* resultSet = cass_future_get_result(executeFuture);
  if (!resultSet) {
    LOG(FATAL) << "Query Execution Successful with NULL Result: This should never happen\n" << Utilities::getStackTrace();
  }

  //Free up the resources from the future and statement (if requested)
  cass_future_free(executeFuture);
  if (isInvalidated) {
    cass_statement_free(statement);
  }

  //Return the result set (or free up the resources)
  if (isResultSetRequested) {
    return resultSet;
  }
  cass_result_free(resultSet);
  return NULL;

}

void CQLConnection::executeBatchUntilPass(CassBatch* batch, CassConsistency consistencyLevel /* = CASS_CONSISTENCY_ONE */) {
  //Set batch consistency level
  cass_batch_set_consistency(batch, consistencyLevel);

  //Execute the statement
  CassFuture* executeFuture = NULL;
  while (true) {
    executeFuture = cass_session_execute_batch(session_, batch);
    cass_future_wait(executeFuture);

    //Determine if the batch statement was executed
    CassError resultCode = cass_future_error_code(executeFuture);
    if (resultCode == CASS_OK) {
      break;
    } else if (resultCode == CASS_ERROR_LIB_NO_HOSTS_AVAILABLE) {
      //Log a message and sleep for one second before trying again
      LOG(DEBUG) << "NHAE:sleep "; //TODO: Include query string?
      Utilities::sleep(ONE_SECOND_SLEEP);
    } else if (resultCode == CASS_ERROR_LIB_REQUEST_TIMED_OUT) {
      //Log a message and sleep for one second before trying again
      LOG(DEBUG) << "RTO:sleep "; //TODO: Include query string?
      Utilities::sleep(ONE_SECOND_SLEEP);
    } else {
      //Log the error message
      CassString message = cass_future_error_message(executeFuture);
      LOG(WARNING) << std::string(message.data, message.length) << "\n" << Utilities::getStackTrace();
    }
  }

  //Free up the resources from the future and batch
  cass_future_free(executeFuture);
  cass_batch_free(batch);
}

bool CQLConnection::isResultSetEmpty(const CassResult* resultSet, bool isInvalidated /* = true */) {
  //Determine if data exists in the result set
  bool isResultSetEmpty = true;
  CassIterator* rows = cass_iterator_from_result(resultSet);
  while(cass_iterator_next(rows)) {
    //Check to see if the row contains data
    const CassRow* row = cass_iterator_get_row(rows);
    if (row) {
      isResultSetEmpty = false;
      break;
    }
  }

  //Free the resources from the result set (if requested) and rows
  if (isInvalidated) {
    cass_result_free(resultSet);
  }
  cass_iterator_free(rows);

  //Indicate whether or not data was present
  return isResultSetEmpty;
}

CQLConnection::CQLConnection(const char* host, bool isSSL, bool isOverwriteLog, CassLogLevel logLevel) : cluster_(NULL), session_(NULL), loggingStream_(NULL) {
  //Initialize logger for the cpp-driver
  uv_mutex_init(&driverLogginglock_);
  cass_log_set_level(logLevel);
  if (logLevel != CASS_LOG_DISABLED) {
    //Determine if the log file should be overwritten
    if (isOverwriteLog) {
      loggingStream_ = new std::fstream(DEFAULT_DRIVER_LOGGING_FILENAME, std::fstream::out | std::fstream::trunc);
    } else {
      loggingStream_ = new std::fstream(DEFAULT_DRIVER_LOGGING_FILENAME, std::fstream::out | std::fstream::app);
    }

    //Assign the driver logging callback handler
    if (!loggingStream_->fail()) {
      cass_log_set_callback(handleDriverLogging, loggingStream_);
    } else {
      LOG(WARNING) << "Unable to Create Driver Log File: " << DEFAULT_DRIVER_LOGGING_FILENAME;
      cass_log_set_level(CASS_LOG_DISABLED);
    }
  }

  //Create the cluster and set the host contact point
  CassFuture* connectFuture = NULL;
  cluster_ = cass_cluster_new();
  session_ = cass_session_new();
  cass_cluster_set_contact_points(cluster_, host);
  cass_cluster_set_num_threads_io(cluster_, DEFAULT_NUMBER_OF_IO_WORKER_THREADS);
  cass_cluster_set_queue_size_io(cluster_, DEFAULT_IO_QUEUE_SIZE);
  cass_cluster_set_pending_requests_low_water_mark(cluster_, DEFAULT_PENDING_REQUESTS_LOW_WATER_MARK);
  cass_cluster_set_pending_requests_high_water_mark(cluster_, DEFAULT_PENDING_REQUESTS_HIGH_WATER_MARK);
  cass_cluster_set_core_connections_per_host(cluster_, DEFAULT_CORE_CONNECTIONS_PER_HOST);
  cass_cluster_set_max_connections_per_host(cluster_, DEFAULT_MAX_CONNECTIONS_PER_HOST);

  //TODO: Handle SSL

  //Perform the connection to the cluster
  LVERBOSE(2) << "Establishing Connection to Cluster";
  connectFuture = cass_session_connect(session_, cluster_);
  CassError errorCode = cass_future_error_code(connectFuture);
  cass_future_free(connectFuture);
  if (errorCode != CASS_OK) {
    throw CQLConnectionException("Unable to connect to the cluster: " + std::string(cass_error_desc(errorCode)));
  }
  LVERBOSE(2) << "Connection to Cluster Established";

  //Determine the version of Cassandra
  determineCassandraVersion();
}

CQLConnection::~CQLConnection() {
  //Close the session object
  CassFuture* closeSessionFuture = cass_session_close(session_);
  cass_future_wait(closeSessionFuture);
  cass_future_free(closeSessionFuture);

  //Close the connection to the cluster
  cass_cluster_free(cluster_);
  cass_session_free(session_);

  //Cleanup the driver logging
  if (loggingStream_) {
    cass_log_cleanup();
    loggingStream_->close();
    delete loggingStream_;
    loggingStream_ = NULL;
  }
}

CassVersion CQLConnection::getCassandraVersion() {
  return version_;
}
