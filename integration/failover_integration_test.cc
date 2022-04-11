/*
 * AWS ODBC Driver for MySQL
 * Copyright Amazon.com Inc. or affiliates.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation and/or
 * other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "base_failover_integration_test.cc"

class FailoverIntegrationTest : public BaseFailoverIntegrationTest {
  protected:
  std::string ACCESS_KEY = std::getenv("AWS_ACCESS_KEY_ID");
  std::string SECRET_ACCESS_KEY = std::getenv("AWS_SECRET_ACCESS_KEY");
  std::string SESSION_TOKEN = std::getenv("AWS_SESSION_TOKEN");
  Aws::Auth::AWSCredentials credentials = Aws::Auth::AWSCredentials(Aws::String(ACCESS_KEY),
                                                                    Aws::String(SECRET_ACCESS_KEY),
                                                                    Aws::String(SESSION_TOKEN));
  Aws::Client::ClientConfiguration client_config;
  Aws::RDS::RDSClient rds_client;
  SQLHENV env;
  SQLHDBC dbc;

  static void SetUpTestSuite() {
    Aws::InitAPI(options);
  }

  static void TearDownTestSuite() {
    Aws::ShutdownAPI(options);
  }

  void SetUp() override {
    SQLAllocHandle(SQL_HANDLE_ENV, nullptr, &env);
    SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0);
    SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc);
    client_config.region = "us-east-2";
    rds_client = Aws::RDS::RDSClient(credentials, client_config);

    cluster_instances = retrieve_topology_via_SDK(rds_client, cluster_id);
    writer_id = get_writer_id(cluster_instances);
    writer_endpoint = get_endpoint(writer_id);
    readers = get_readers(cluster_instances);
    reader_id = get_first_reader_id(cluster_instances);
    reader_endpoint = get_proxied_endpoint(reader_id);
  }

  void TearDown() override {
    if (nullptr != dbc) {
      SQLFreeHandle(SQL_HANDLE_DBC, dbc);
    }
    if (nullptr != env) {
      SQLFreeHandle(SQL_HANDLE_ENV, env);
    }
  }
};

/**
* Current writer dies, a reader instance is nominated to be a new writer, failover to the new
* writer. Driver failover occurs when executing a method against the connection
*/
TEST_F(FailoverIntegrationTest, test_failFromWriterToNewWriter_failOnConnectionInvocation) {
  build_connection_string(conn_in, dsn, user, pwd, writer_endpoint, MYSQL_PORT, db);
  SQLCHAR conn_out[4096];
  SQLSMALLINT len;

  EXPECT_EQ(SQL_SUCCESS, SQLDriverConnect(dbc, nullptr, conn_in, SQL_NTS, conn_out, MAX_NAME_LEN, &len, SQL_DRIVER_NOPROMPT));

  failover_cluster_and_wait_until_writer_changed(rds_client, cluster_id, writer_id);
  assert_query_failed(dbc, SERVER_ID_QUERY, ERROR_COMM_LINK_CHANGED);
  
  const std::string current_connection_id = query_instance_id(dbc);
  EXPECT_TRUE(is_DB_instance_writer(rds_client, cluster_id, current_connection_id));
  EXPECT_NE(current_connection_id, writer_id);

  EXPECT_EQ(SQL_SUCCESS, SQLDisconnect(dbc));
}

TEST_F(FailoverIntegrationTest, test_takeOverConnectionProperties) {
  SQLCHAR conn_out[4096], message[SQL_MAX_MESSAGE_LENGTH];
  SQLINTEGER native_error;
  SQLSMALLINT len;

  // Establish the topology cache so that we can later assert that new connections does not inherit properties from
  // cached connection either before or after failover
  sprintf(
      reinterpret_cast<char*>(conn_in),
      "DSN=%s;UID=%s;PWD=%s;SERVER=%s;PORT=%d;LOG_QUERY=1;MULTI_STATEMENTS=0;",
      dsn, user, pwd, MYSQL_CLUSTER_URL.c_str(), MYSQL_PORT);
  EXPECT_EQ(SQL_SUCCESS, SQLDriverConnect(dbc, nullptr, conn_in, SQL_NTS, conn_out, MAX_NAME_LEN, &len, SQL_DRIVER_NOPROMPT));
  EXPECT_EQ(SQL_SUCCESS, SQLDisconnect(dbc));

  sprintf(
    reinterpret_cast<char*>(conn_in),
    "DSN=%s;UID=%s;PWD=%s;SERVER=%s;PORT=%d;LOG_QUERY=1;MULTI_STATEMENTS=1;",
    dsn, user, pwd, MYSQL_CLUSTER_URL.c_str(), MYSQL_PORT);
  EXPECT_EQ(SQL_SUCCESS, SQLDriverConnect(dbc, nullptr, conn_in, SQL_NTS, conn_out, MAX_NAME_LEN, &len, SQL_DRIVER_NOPROMPT));

  SQLHSTMT handle;
  SQLSMALLINT stmt_length;
  SQLCHAR stmt_sqlstate[6];
  const auto query = (SQLCHAR*)"select @@aurora_server_id; select 1; select 2;";

  EXPECT_EQ(SQL_SUCCESS, SQLAllocHandle(SQL_HANDLE_STMT, dbc, &handle));

  // Verify that connection accepts multi-statement sql
  EXPECT_EQ(SQL_SUCCESS, SQLExecDirect(handle, query, SQL_NTS));

  failover_cluster_and_wait_until_writer_changed(rds_client, cluster_id, writer_id);

  EXPECT_EQ(SQL_ERROR, SQLExecDirect(handle, SERVER_ID_QUERY, SQL_NTS));
  EXPECT_EQ(SQL_SUCCESS, SQLError(env, dbc, handle, stmt_sqlstate, &native_error, message, SQL_MAX_MESSAGE_LENGTH - 1, &stmt_length));
  const std::string state = reinterpret_cast<char*>(stmt_sqlstate);
  EXPECT_EQ(ERROR_COMM_LINK_CHANGED, state);

  // Verify that connection still accepts multi-statement SQL
  EXPECT_EQ(SQL_SUCCESS, SQLExecDirect(handle, query, SQL_NTS));

  EXPECT_EQ(SQL_SUCCESS, SQLFreeHandle(SQL_HANDLE_STMT, handle));
  EXPECT_EQ(SQL_SUCCESS, SQLDisconnect(dbc));
}

/** Writer fails within a transaction. Open transaction with "SET autocommit = 0" */
TEST_F(FailoverIntegrationTest, test_writerFailWithinTransaction_setAutocommitSqlZero) {
  build_connection_string(conn_in, dsn, user, pwd, writer_endpoint, MYSQL_PORT, db);
  SQLCHAR conn_out[4096], message[SQL_MAX_MESSAGE_LENGTH];
  SQLINTEGER native_error;
  SQLSMALLINT len;

  EXPECT_EQ(SQL_SUCCESS, SQLDriverConnect(dbc, nullptr, conn_in, SQL_NTS, conn_out, MAX_NAME_LEN, &len, SQL_DRIVER_NOPROMPT));

  // Setup tests
  SQLHSTMT handle;
  SQLSMALLINT stmt_length;
  SQLCHAR stmt_sqlstate[6];
  EXPECT_EQ(SQL_SUCCESS, SQLAllocHandle(SQL_HANDLE_STMT, dbc, &handle));
  const auto drop_table_query = (SQLCHAR*)"DROP TABLE IF EXISTS test3_1"; // Setting up tables
  const auto create_table_query = (SQLCHAR*)"CREATE TABLE test3_1 (id INT NOT NULL PRIMARY KEY, test3_1_field VARCHAR(255) NOT NULL)"; 
  const auto setup_autocommit_query = (SQLCHAR*)"SET autocommit = 0"; // Open a new transaction

  // Execute setup query
  EXPECT_EQ(SQL_SUCCESS, SQLExecDirect(handle, drop_table_query, SQL_NTS));
  EXPECT_EQ(SQL_SUCCESS, SQLExecDirect(handle, create_table_query, SQL_NTS));
  EXPECT_EQ(SQL_SUCCESS, SQLExecDirect(handle, setup_autocommit_query, SQL_NTS));

  // Execute queries within the transaction
  const auto insert_query_A = (SQLCHAR*)"INSERT INTO test3_1 VALUES (1, 'test field string 1')"; 
  EXPECT_EQ(SQL_SUCCESS, SQLExecDirect(handle, insert_query_A, SQL_NTS));

  failover_cluster_and_wait_until_writer_changed(rds_client, cluster_id, writer_id);

  // If there is an active transaction (The insert queries), roll it back and return an error 08007.
  EXPECT_EQ(SQL_ERROR, SQLEndTran(SQL_HANDLE_DBC, dbc, SQL_COMMIT));

  // Check state
  EXPECT_EQ(SQL_SUCCESS, SQLError(env, dbc, nullptr, stmt_sqlstate, &native_error, message, SQL_MAX_MESSAGE_LENGTH - 1, &stmt_length));
  const std::string state = (char*)stmt_sqlstate;
  EXPECT_EQ(ERROR_CONN_FAILURE_DURING_TX, state);

  // Query new ID after failover
  std::string current_connection_id = query_instance_id(dbc);

  // Check if current connection is a new writer
  EXPECT_TRUE(is_DB_instance_writer(rds_client, cluster_id, current_connection_id));
  EXPECT_NE(current_connection_id, writer_id);

  // No rows should have been inserted to the table
  EXPECT_EQ(0, query_count_table_rows(handle, "test3_1"));

  // Clean up test
  EXPECT_EQ(SQL_SUCCESS, SQLExecDirect(handle, drop_table_query, SQL_NTS));
  EXPECT_EQ(SQL_SUCCESS, SQLDisconnect(dbc));
}

/** Writer fails within a transaction. Open transaction with SQLSetConnectAttr */
TEST_F(FailoverIntegrationTest, test_writerFailWithinTransaction_setAutoCommitFalse) {
  build_connection_string(conn_in, dsn, user, pwd, writer_endpoint, MYSQL_PORT, db);
  SQLCHAR conn_out[4096], message[SQL_MAX_MESSAGE_LENGTH];
  SQLINTEGER native_error;
  SQLSMALLINT len;

  EXPECT_EQ(SQL_SUCCESS, SQLDriverConnect(dbc, nullptr, conn_in, SQL_NTS, conn_out, MAX_NAME_LEN, &len, SQL_DRIVER_NOPROMPT));

  // Setup tests
  SQLHSTMT handle;
  SQLSMALLINT stmt_length;
  SQLCHAR stmt_sqlstate[6];
  EXPECT_EQ(SQL_SUCCESS, SQLAllocHandle(SQL_HANDLE_STMT, dbc, &handle));
  const auto drop_table_query = (SQLCHAR*)"DROP TABLE IF EXISTS test3_2"; // Setting up tables
  const auto create_table_query = (SQLCHAR*)"CREATE TABLE test3_2 (id INT NOT NULL PRIMARY KEY, test3_2_field VARCHAR(255) NOT NULL)";

  // Execute setup query
  EXPECT_EQ(SQL_SUCCESS, SQLExecDirect(handle, drop_table_query, SQL_NTS));
  EXPECT_EQ(SQL_SUCCESS, SQLExecDirect(handle, create_table_query, SQL_NTS));

  // Set autocommit = false
  EXPECT_EQ(SQL_SUCCESS, SQLSetConnectAttr(dbc, SQL_ATTR_AUTOCOMMIT, SQL_AUTOCOMMIT_OFF, 0));

  // Execute queries within the transaction
  const auto insert_query_A = (SQLCHAR*)"INSERT INTO test3_2 VALUES (1, 'test field string 1')"; 
  EXPECT_EQ(SQL_SUCCESS, SQLExecDirect(handle, insert_query_A, SQL_NTS));

  failover_cluster_and_wait_until_writer_changed(rds_client, cluster_id, writer_id);
    
  // If there is an active transaction, roll it back and return an error 08007.
  EXPECT_EQ(SQL_ERROR, SQLEndTran(SQL_HANDLE_DBC, dbc, SQL_COMMIT));

  // Check state
  EXPECT_EQ(SQL_SUCCESS, SQLError(env, dbc, nullptr, stmt_sqlstate, &native_error, message, SQL_MAX_MESSAGE_LENGTH - 1, &stmt_length));
  const std::string state = (char*)stmt_sqlstate;
  EXPECT_EQ(ERROR_CONN_FAILURE_DURING_TX, state);

  // Query new ID after failover
  std::string current_connection_id = query_instance_id(dbc);

  // Check if current connection is a new writer
  EXPECT_TRUE(is_DB_instance_writer(rds_client, cluster_id, current_connection_id));
  EXPECT_NE(current_connection_id, writer_id);

  // No rows should have been inserted to the table
  EXPECT_EQ(0, query_count_table_rows(handle, "test3_2"));

  // Clean up test
  EXPECT_EQ(SQL_SUCCESS, SQLExecDirect(handle, drop_table_query, SQL_NTS));
  EXPECT_EQ(SQL_SUCCESS, SQLDisconnect(dbc));
}

/** Writer fails within a transaction. Open transaction with "START TRANSACTION". */
TEST_F(FailoverIntegrationTest, test_writerFailWithinTransaction_startTransaction) {
  build_connection_string(conn_in, dsn, user, pwd, writer_endpoint, MYSQL_PORT, db);
  SQLCHAR conn_out[4096], message[SQL_MAX_MESSAGE_LENGTH];
  SQLINTEGER native_error;
  SQLSMALLINT len;

  // Connect to writer
  EXPECT_EQ(SQL_SUCCESS, SQLDriverConnect(dbc, nullptr, conn_in, SQL_NTS, conn_out, MAX_NAME_LEN, &len, SQL_DRIVER_NOPROMPT));

  // Setup tests
  SQLHSTMT handle;
  SQLSMALLINT stmt_length;
  SQLCHAR stmt_sqlstate[6];
  EXPECT_EQ(SQL_SUCCESS, SQLAllocHandle(SQL_HANDLE_STMT, dbc, &handle));
  const auto drop_table_query = (SQLCHAR*)"DROP TABLE IF EXISTS test3_3"; // Setting up tables
  const auto create_table_query = (SQLCHAR*)"CREATE TABLE test3_3 (id INT NOT NULL PRIMARY KEY, test3_3_field VARCHAR(255) NOT NULL)"; 
  const auto start_trans_query = (SQLCHAR*)"START TRANSACTION"; // Open a new transaction

  // Execute setup query
  EXPECT_EQ(SQL_SUCCESS, SQLExecDirect(handle, drop_table_query, SQL_NTS));
  EXPECT_EQ(SQL_SUCCESS, SQLExecDirect(handle, create_table_query, SQL_NTS));
  EXPECT_EQ(SQL_SUCCESS, SQLExecDirect(handle, start_trans_query, SQL_NTS));

  // Execute queries within the transaction
  const auto insert_query_A = (SQLCHAR*)"INSERT INTO test3_3 VALUES (1, 'test field string 1')"; 
  EXPECT_EQ(SQL_SUCCESS, SQLExecDirect(handle, insert_query_A, SQL_NTS));

  failover_cluster_and_wait_until_writer_changed(rds_client, cluster_id, writer_id);

  // If there is an active transaction (The insert queries), roll it back and return an error 08007.
  EXPECT_EQ(SQL_ERROR, SQLEndTran(SQL_HANDLE_DBC, dbc, SQL_COMMIT));

  // Check state
  EXPECT_EQ(SQL_SUCCESS, SQLError(env, dbc, nullptr, stmt_sqlstate, &native_error, message, SQL_MAX_MESSAGE_LENGTH - 1, &stmt_length));
  const std::string state = (char*)stmt_sqlstate;
  EXPECT_EQ(ERROR_CONN_FAILURE_DURING_TX, state);

  // Query new ID after failover
  std::string current_connection_id = query_instance_id(dbc);

  // Check if current connection is a new writer
  EXPECT_TRUE(is_DB_instance_writer(rds_client, cluster_id, current_connection_id));
  EXPECT_NE(current_connection_id, writer_id);

  // No rows should have been inserted to the table
  EXPECT_EQ(0, query_count_table_rows(handle, "test3_3"));

  // Clean up test
  EXPECT_EQ(SQL_SUCCESS, SQLExecDirect(handle, drop_table_query, SQL_NTS));
  EXPECT_EQ(SQL_SUCCESS, SQLDisconnect(dbc));
}

/* Writer fails within NO transaction. */
TEST_F(FailoverIntegrationTest, test_writerFailWithNoTransaction) {
  build_connection_string(conn_in, dsn, user, pwd, writer_endpoint, MYSQL_PORT, db);
  SQLCHAR conn_out[4096], message[SQL_MAX_MESSAGE_LENGTH];
  SQLINTEGER native_error;
  SQLSMALLINT len;

  EXPECT_EQ(SQL_SUCCESS, SQLDriverConnect(dbc, nullptr, conn_in, SQL_NTS, conn_out, MAX_NAME_LEN, &len, SQL_DRIVER_NOPROMPT));

  // Setup tests
  SQLHSTMT handle;
  SQLSMALLINT stmt_length;
  SQLCHAR stmt_sqlstate[6];
  EXPECT_EQ(SQL_SUCCESS, SQLAllocHandle(SQL_HANDLE_STMT, dbc, &handle));
  const auto drop_table_query = (SQLCHAR*)"DROP TABLE IF EXISTS test3_4"; // Setting up tables
  const auto setup_table_query = (SQLCHAR*)"CREATE TABLE test3_4 (id int not null primary key, test3_2_field varchar(255) not null)";

  // Execute setup query
  EXPECT_EQ(SQL_SUCCESS, SQLExecDirect(handle, drop_table_query, SQL_NTS));
  EXPECT_EQ(SQL_SUCCESS, SQLExecDirect(handle, setup_table_query, SQL_NTS));

  // Have something inserted into table
  EXPECT_EQ(SQL_SUCCESS, SQLAllocHandle(SQL_HANDLE_STMT, dbc, &handle));
  const auto insert_query_A = (SQLCHAR*)"INSERT INTO test3_4 VALUES (1, 'test field string 1')"; 
  EXPECT_EQ(SQL_SUCCESS, SQLExecDirect(handle, insert_query_A, SQL_NTS));

  failover_cluster_and_wait_until_writer_changed(rds_client, cluster_id, writer_id);

  // Query expected to fail and rollback things in transaction
  const auto insert_query_B = (SQLCHAR*)"INSERT INTO test3_4 VALUES (2, 'test field string 2')";

  // Execute query expecting failure & rollback insert 2
  EXPECT_EQ(SQL_ERROR, SQLExecDirect(handle, insert_query_B, SQL_NTS)); // Triggers failover

  // Check state
  EXPECT_EQ(SQL_SUCCESS, SQLError(env, dbc, handle, stmt_sqlstate, &native_error, message, SQL_MAX_MESSAGE_LENGTH - 1, &stmt_length));
  const std::string state = (char*)stmt_sqlstate;
  EXPECT_EQ(ERROR_COMM_LINK_CHANGED, state);

  // Query new ID after failover
  std::string current_connection_id = query_instance_id(dbc);

  // Check if current connection is a new writer
  EXPECT_TRUE(is_DB_instance_writer(rds_client, cluster_id, current_connection_id));
  EXPECT_NE(current_connection_id, writer_id);

  // ID 1 should have 1 row, ID 2 should have NO rows
  EXPECT_EQ(1, query_count_table_rows(handle, "test3_4", 1));
  EXPECT_EQ(0, query_count_table_rows(handle, "test3_4", 2));

  // Clean up test
  EXPECT_EQ(SQL_SUCCESS, SQLExecDirect(handle, drop_table_query, SQL_NTS));
  EXPECT_EQ(SQL_SUCCESS, SQLDisconnect(dbc));
}

/**
 * Current reader dies, no other reader instance, failover to writer, then writer dies, failover
 * to another available reader instance.
 */
TEST_F(FailoverIntegrationTest, test_failFromReaderToWriterToAnyAvailableInstance) {
  // Ensure all networks to instances are enabled
  for (const auto& x : proxy_map) {
    enable_connectivity(x.second);
  }

  // Disable all readers but one & writer
  for (size_t index = 1; index < readers.size(); ++index) {
    disable_instance(readers[index]);
  }

  const std::string initial_writer_id = writer_id;
  const std::string initial_reader_id = reader_id;
  const std::string initial_reader_endpoint = get_proxied_endpoint(initial_reader_id);

  SQLCHAR conn_out[4096];
  SQLSMALLINT len;
  
  sprintf(reinterpret_cast<char*>(conn_in), "%sSERVER=%s;PORT=%d;ALLOW_READER_CONNECTIONS=1;", get_default_proxied_config().c_str(), initial_reader_endpoint.c_str(), MYSQL_PROXY_PORT);
  EXPECT_EQ(SQL_SUCCESS, SQLDriverConnect(dbc, nullptr, conn_in, SQL_NTS, conn_out, MAX_NAME_LEN, &len, SQL_DRIVER_NOPROMPT));

  disable_instance(initial_reader_id);

  assert_query_failed(dbc, SERVER_ID_QUERY, ERROR_COMM_LINK_CHANGED);

  std::string current_connection = query_instance_id(dbc);
  EXPECT_EQ(current_connection, initial_writer_id);

  // Re-enable 2 readers (Second & Third reader)
  auto second_reader_id = readers[1];
  auto third_reader_id = readers[2];
  enable_instance(second_reader_id);
  enable_instance(third_reader_id);

  failover_cluster_and_wait_until_writer_changed(rds_client, cluster_id, initial_writer_id);

  // Query to trigger failover (Initial Writer)
  assert_query_failed(dbc, SERVER_ID_QUERY, ERROR_COMM_LINK_CHANGED);

  // Expect that we're connected to reader 2 or 3
  current_connection = query_instance_id(dbc);
  EXPECT_TRUE(current_connection == second_reader_id || current_connection == third_reader_id);

  EXPECT_EQ(SQL_SUCCESS, SQLDisconnect(dbc));
}

/* Pooled connection tests. */

/* Writer connection failover within the connection pool. */
TEST_F(FailoverIntegrationTest, test_pooledWriterConnection_BasicFailover) {
  const auto nominated_writer_id = cluster_instances[1];

  // Enable connection pooling
  EXPECT_EQ(SQL_SUCCESS, SQLSetEnvAttr(NULL, SQL_ATTR_CONNECTION_POOLING, (SQLPOINTER)SQL_CP_ONE_PER_DRIVER, 0));
  EXPECT_EQ(SQL_SUCCESS, SQLSetEnvAttr(env, SQL_ATTR_CP_MATCH, reinterpret_cast<SQLPOINTER>(SQL_CP_STRICT_MATCH), 0));

  build_connection_string(conn_in, dsn, user, pwd, writer_endpoint, MYSQL_PORT, db);
  SQLCHAR conn_out[4096], message[SQL_MAX_MESSAGE_LENGTH];
  SQLINTEGER native_error;
  SQLSMALLINT len;

  SQLRETURN rc = SQLDriverConnect(dbc, nullptr, conn_in, SQL_NTS, conn_out, MAX_NAME_LEN, &len, SQL_DRIVER_NOPROMPT);
  if ((rc != SQL_SUCCESS) && (rc != SQL_SUCCESS_WITH_INFO)) {
    FAIL();
  }

  failover_cluster_and_wait_until_writer_changed(rds_client, cluster_id, writer_id, nominated_writer_id);
  assert_query_failed(dbc, SERVER_ID_QUERY, ERROR_COMM_LINK_CHANGED);
  
  const std::string current_connection_id = query_instance_id(dbc);
  const std::string next_writer_id = get_DB_cluster_writer_instance_id(rds_client, cluster_id);
  EXPECT_TRUE(is_DB_instance_writer(rds_client, cluster_id, current_connection_id));
  EXPECT_EQ(next_writer_id, current_connection_id);
  EXPECT_EQ(nominated_writer_id, current_connection_id);
  EXPECT_EQ(SQL_SUCCESS, SQLDisconnect(dbc));
}
