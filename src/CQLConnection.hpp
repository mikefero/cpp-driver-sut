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
#ifndef _CQL_CONNECTION_HPP
#define _CQL_CONNECTION_HPP
#include <memory.h>

#include <cassandra.h>
#include <uv.h>

#include "CQLConnectionException.hpp"

/**
 * Cassandra release version number
 */
struct CassVersion {
  /**
   * Major portion of version number
   */
  unsigned short major;
  /**
   * Minor portion of version number
   */
  unsigned short minor;
  /**
   * Patch portion of version number
   */
  unsigned short patch;
  /**
   * Extra portion of version number
   */
  char extra[64];

  /**
   * Initializing constructor for structure
   */
  CassVersion() {
    major = 0;
    minor = 0;
    patch = 0;
    memset(extra, '\0', sizeof(extra));
  };
};

/**
 * CQL connection class for performing standard operations on a Cassandra
 * database
 */
class CQLConnection {
private:
  /**
   * Formatted CQL string for obtaining the Cassandra release version
   */
  const static char* SELECT_CASSANDRA_RELEASE_VERSION;
  /**
   * Cluster connection is the main entry point for the cpp-driver to
   * discover nodes during the session
   */
  CassCluster* cluster_;
  /**
   * Session holds the connections to a Cassandra cluster in order to perform
   * operations on the database
   */
  CassSession* session_;
  /**
   * Cassandra version retrieved from a connected session
   */
  CassVersion version_;
  /**
   * Logging filestream to output driver logging messages
   */
  std::fstream* loggingStream_;
  /**
   * Mutex for operations for the logging callback
   */
  static uv_mutex_t driverLogginglock_;

public:
  /**
  * Determine the Cassandra version from the connected session
  */
  void determineCassandraVersion();
  /**
   * Create a prepared statement for later use
   *
   * @param query Query to create prepared statement
   * @return Prepared statement
   */
  CassPrepared* createPreparedStatement(const char* query);
  /**
   * Create a statement for later use
   *
   * @param query Query to create statement
   * @param count Number of variable arguments
   *              (default: 0)
   * @param args Variable arguments for the formatted CQL string
   * @return Regular simple statement
   */
  CassStatement* createStatement(const char* query, int count = 0, ...);
  /**
   * Execute a variable formatted CQL query string at a certain consistency
   * level
   *
   * @param query Formatted CQL query string
   * @param consistencyLevel Consistency level to execute the query at
   *                         (default: CASS_CONSISTENCY_ONE)
   * @param count Number of variable arguments
   *              (default: 0)
   * @param args Variable arguments for the formatted CQL string
   * @return Error code for statement execution
   */
  CassError executeQuery(const char* query, CassConsistency consistencyLevel = CASS_CONSISTENCY_ONE, int count = 0, ...);
  /**
   * Execute a statement
   *
   * @param statement Statement to execute (becomes invalidated after
   *                  execution; freed)
   * @param isInvalidated True if statement should be invalidated (freed);
   *                      false otherwise (default: true)
   * @param consistencyLevel Consistency level to execute the statement at
   *                         (default: CASS_CONSISTENCY_ONE)
   * @return Error code for statement execution
   */
  CassError executeStatement(CassStatement* statement, bool isInvalidated = true, CassConsistency consistencyLevel = CASS_CONSISTENCY_ONE);
  /**
   * Execute a statement until it passes
   *
   * @param statement Statement to execute (becomes invalidated after
   *                  execution; freed)
   * @param isResultSetRequested True if result set should be returned; false
   *                             otherwise (default: false).  Result set
   *                             resources must be freed if requested.
   * @param isInvalidated True if statement should be invalidated (freed);
   *                      false otherwise (default: true)
   * @param consistencyLevel Consistency level to execute the statement at
   *                         (default: CASS_CONSISTENCY_ONE)
   * @return Result set returned from the executing statement; NULL if result
   *         set was not requested.
   */
  const CassResult* executeStatementUntilPass(CassStatement* statement, bool isResultSetRequested = false, bool isInvalidated = true, CassConsistency consistencyLevel = CASS_CONSISTENCY_ONE);
  /**
   * Execute a batch until it passes
   *
   * @param batch Batch to execute (becomes invalidated after execution;
   *              freed)
   * @param consistencyLevel Consistency level to execute the batch at
   *                         (default: CASS_CONSISTENCY_ONE)
   */
  void executeBatchUntilPass(CassBatch* batch, CassConsistency consistencyLevel = CASS_CONSISTENCY_ONE);
  /**
   * Check to determine if a given result set it empty
   *
   * @param resultSet Result set to check
   * @param isInvalidated True if result set should be invalidated (freed);
   *                      false otherwise (default: true)
   * @return True if result set is empty; false otherwise
   */
  bool isResultSetEmpty(const CassResult* resultSet, bool isInvalidated = true);
  /**
   * Handle driver logging messages and output them to a file
   *
   * @param message Driver log message to output
   * @param data An opaque data object passed to the callback (NOT USED)
   */
  static void handleDriverLogging(const CassLogMessage* message, void* data);

public:
  /**
   * Constructor
   *
   * @param host Host/Node to establish cluster connection
   * @param isSSL True if SSL should be enabled; otherwise SSL will be
   *              disabled
   * @param isOverwriteLog True if driver log file should be overwritten;
   *                       false otherwise
   * @param logLevel Driver logging level to apply
   * @throws CQLExeception <ul><li>if unable to connect to cluster or create
   *                       session object</li><li>if the schema could not be
   *                       created</li><li>if table is not set or prepared
   *                       could not be created</li></ul>
   */
  CQLConnection(const char* host, bool isSSL, bool isOverwriteLog, CassLogLevel logLevel);
  /**
   * Destructor
   */
  ~CQLConnection();
  /**
   * Get the Cassandra version from the connected session
   *
   * @return CassVersion structure (major, minor, patch, extra)
   */
  CassVersion getCassandraVersion();


};

#endif //_CQL_CONNECTION_HPP
