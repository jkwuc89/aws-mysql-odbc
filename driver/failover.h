/**
  @file failover.h
  @brief Definitions needed for failover
*/

#ifndef __FAILOVER_H__
#define __FAILOVER_H__

#include "driver.h"

#include <stddef.h>
#include <vector>
#include <list>
#include <string>
#include <map>
#include <set>
#include <ctime>
#include <functional>
#include <condition_variable>

#include <mysql.h>

struct DBC;

enum HOST_STATE { UP, DOWN };

// TODO Think about char types. Using strings for now, but should SQLCHAR *, or CHAR * be employed?
// Most of the strings are for internal failover things
struct HOST_INFO {
    HOST_INFO();
    //TODO - probably choose one of the following constructors, or more precisely choose which data type they should take
    HOST_INFO(std::string host, int port);
    HOST_INFO(const char* host, int port);
    const int NO_PORT = -1;

    int get_port();
    std::string get_host();
    std::string get_host_port_pair();
    bool equal_host_port_pair(HOST_INFO& hi);
    HOST_STATE get_host_state();
    void set_host_state(HOST_STATE state);
    bool is_host_up();
    bool is_host_down();
    bool is_host_writer();
    void mark_as_writer(bool writer);
    static bool is_host_same(const std::shared_ptr<HOST_INFO>& h1, const std::shared_ptr<HOST_INFO>& h2);

    // used to be properties - TODO - remove the not needed one's
    std::string session_id;
    std::string last_updated;
    std::string replica_lag;
    std::string instance_name;

private:
    const std::string HOST_PORT_SEPARATOR = ":";  
    const std::string host;
    const int port;

    HOST_STATE host_state;
    bool is_writer;
};

// This class holds topology information for one cluster.
// Cluster topology consists of an instance endpoint, a set of nodes in the cluster,
// the type of each node in the cluster, and the status of each node in the cluster.
class CLUSTER_TOPOLOGY_INFO {
public:
    CLUSTER_TOPOLOGY_INFO();
    CLUSTER_TOPOLOGY_INFO(const CLUSTER_TOPOLOGY_INFO& src_info); //copy constructor
    virtual ~CLUSTER_TOPOLOGY_INFO();

    void add_host(std::shared_ptr<HOST_INFO> host_info);
    bool is_multi_writer_cluster();
    int total_hosts();
    int num_readers(); // return number of readers in the cluster
    std::time_t time_last_updated();

    std::shared_ptr<HOST_INFO> get_writer();
    std::shared_ptr<HOST_INFO> get_next_reader();
    // TODO - Ponder if the get_reader below is needed. In general user of this should not need to deal with indexes.
    // One case that comes to mind, if we were to try to do a random shuffle of readers or hosts in general like JDBC driver
    // we could do random shuffle of host indices and call the get_reader for specific index in order we wanted.
    std::shared_ptr<HOST_INFO> get_reader(int i);
    std::vector<std::shared_ptr<HOST_INFO>> get_writers();
    std::vector<std::shared_ptr<HOST_INFO>> get_readers();

private:
    int current_reader = -1;
    std::time_t last_updated;
    std::set<std::string> down_hosts; // maybe not needed, HOST_INFO has is_host_down() method
    //std::vector<HOST_INFO*> hosts;
    std::shared_ptr<HOST_INFO> last_used_reader;  // TODO perhaps this overlaps with current_reader and is not needed

    // TODO - can we do without pointers -
    // perhaps ok for now, we are using copies CLUSTER_TOPOLOGY_INFO returned by get_topology and get_cached_topology from TOPOLOGY_SERVICE.
    // However, perhaps smart shared pointers could be used.
    std::vector<std::shared_ptr<HOST_INFO>> writers;
    std::vector<std::shared_ptr<HOST_INFO>> readers;

    std::shared_ptr<HOST_INFO> get_last_used_reader();
    void set_last_used_reader(std::shared_ptr<HOST_INFO> reader);
    void mark_host_down(std::shared_ptr<HOST_INFO> down_host);
    void unmark_host_down(std::shared_ptr<HOST_INFO> host);
    std::set<std::string> get_down_hosts();
    void update_time();

    friend class TOPOLOGY_SERVICE;
};


class TOPOLOGY_SERVICE {
public:
    TOPOLOGY_SERVICE();
    virtual ~TOPOLOGY_SERVICE();

    void set_cluster_id(const char* cluster_id);
    void set_cluster_instance_template(std::shared_ptr<HOST_INFO> host_template);  //is this equivalent to setcluster_instance_host

    std::shared_ptr<CLUSTER_TOPOLOGY_INFO> get_topology(std::shared_ptr<MYSQL> connection, bool force_update = false);
    std::shared_ptr<CLUSTER_TOPOLOGY_INFO> get_cached_topology();

    std::shared_ptr<HOST_INFO> get_last_used_reader();
    void set_last_used_reader(std::shared_ptr<HOST_INFO> reader);
    std::set<std::string> get_down_hosts();
    void mark_host_down(std::shared_ptr<HOST_INFO> down_host);
    void unmark_host_down(std::shared_ptr<HOST_INFO> host);
    void set_refresh_rate(int refresh_rate);
    void clear_all();
    void clear();

    // Property Keys
    const std::string SESSION_ID = "TOPOLOGY_SERVICE_SESSION_ID";
    const std::string LAST_UPDATED = "TOPOLOGY_SERVICE_LAST_UPDATE_TIMESTAMP";
    const std::string REPLICA_LAG = "TOPOLOGY_SERVICE_REPLICA_LAG_IN_MILLISECONDS";
    const std::string INSTANCE_NAME = "TOPOLOGY_SERVICE_SERVER_ID";

private:
    // TODO - consider - do we really need miliseconds for refresh? - the default numbers here are already 30 seconds.
    const int DEFAULT_REFRESH_RATE_IN_MILLISECONDS = 30000;
    const int DEFAULT_CACHE_EXPIRE_MS = 5 * 60 * 1000; // 5 min

    const std::string GET_INSTANCE_NAME_SQL = "SELECT @@aurora_server_id";
    const std::string GET_INSTANCE_NAME_COL = "@@aurora_server_id";
    const std::string WRITER_SESSION_ID = "MASTER_SESSION_ID";

    const std::string FIELD_SERVER_ID = "SERVER_ID";
    const std::string FIELD_SESSION_ID = "SESSION_ID";
    const std::string FIELD_LAST_UPDATED = "LAST_UPDATE_TIMESTAMP";
    const std::string FIELD_REPLICA_LAG = "REPLICA_LAG_IN_MILLISECONDS";

    const char* RETRIEVE_TOPOLOGY_SQL =
        "SELECT SERVER_ID, SESSION_ID, LAST_UPDATE_TIMESTAMP, REPLICA_LAG_IN_MILLISECONDS \
		FROM information_schema.replica_host_status \
		WHERE time_to_sec(timediff(now(), LAST_UPDATE_TIMESTAMP)) <= 300 \
		ORDER BY LAST_UPDATE_TIMESTAMP DESC";

protected:
    const int NO_CONNECTION_INDEX = -1;
    int refresh_rate_in_milliseconds;

    std::string cluster_id;
    std::shared_ptr<HOST_INFO> cluster_instance_host;

    // TODO performance metrics
    // bool gather_perf_Metrics = false;

    std::map<std::string, std::shared_ptr<CLUSTER_TOPOLOGY_INFO>> topology_cache;
    std::mutex topology_cache_mutex;

    bool refresh_needed(std::time_t last_updated);
    std::shared_ptr<CLUSTER_TOPOLOGY_INFO> query_for_topology(std::shared_ptr<MYSQL> connection);
    std::shared_ptr<HOST_INFO> create_host(MYSQL_ROW& row);
    std::string get_host_endpoint(const char* node_name);

    std::shared_ptr<CLUSTER_TOPOLOGY_INFO> get_from_cache();
    void put_to_cache(std::shared_ptr<CLUSTER_TOPOLOGY_INFO> topology_info);
};

class FAILOVER_CONNECTION_HANDLER {
   public:
    FAILOVER_CONNECTION_HANDLER(std::shared_ptr<DBC> dbc);
    virtual ~FAILOVER_CONNECTION_HANDLER();

    std::shared_ptr<MYSQL> connect(std::shared_ptr<HOST_INFO> host_info);
    void update_connection(std::shared_ptr<MYSQL> new_connection);
    void release_connection(std::shared_ptr<MYSQL> mysql);

   private:
    std::shared_ptr<DBC> dbc;

    std::shared_ptr<DBC> clone_dbc(std::shared_ptr<DBC> source_dbc);
    void release_dbc(std::shared_ptr<DBC> dbc_clone);
};

struct READER_FAILOVER_RESULT {
    bool connected;
    std::shared_ptr<HOST_INFO> new_host;
    std::shared_ptr<MYSQL> new_connection;
};

class FAILOVER_READER_HANDLER {
   public:
    FAILOVER_READER_HANDLER(
        std::shared_ptr<TOPOLOGY_SERVICE> topology_service,
        std::shared_ptr<FAILOVER_CONNECTION_HANDLER> connection_handler);
    ~FAILOVER_READER_HANDLER();
    READER_FAILOVER_RESULT failover(
        std::shared_ptr<CLUSTER_TOPOLOGY_INFO> topology_info,
        const std::function<bool()> is_canceled);
    READER_FAILOVER_RESULT get_reader_connection(
        std::shared_ptr<CLUSTER_TOPOLOGY_INFO> topology_info,
        const std::function<bool()> is_canceled);

   private:
    std::shared_ptr<TOPOLOGY_SERVICE> topology_service;
    std::shared_ptr<FAILOVER_CONNECTION_HANDLER> connection_handler;

    std::vector<std::shared_ptr<HOST_INFO>> build_hosts_list(
        const std::shared_ptr<CLUSTER_TOPOLOGY_INFO>& topology_info,
        bool contain_writers);
    READER_FAILOVER_RESULT get_connection_from_hosts(
        std::vector<std::shared_ptr<HOST_INFO>> hosts_list,
        const std::function<bool()> is_canceled);
};

// This struct holds results of Writer Failover Process.
struct WRITER_FAILOVER_RESULT {
    bool connected;
    bool is_new_host;  // True if process connected to a new host. False if
                       // process re-connected to the same host
    std::shared_ptr<CLUSTER_TOPOLOGY_INFO> new_topology;
    std::shared_ptr<MYSQL> new_connection;
};

class FAILOVER_WRITER_HANDLER {
   public:
    FAILOVER_WRITER_HANDLER(
        std::shared_ptr<TOPOLOGY_SERVICE> topology_service,
        std::shared_ptr<FAILOVER_READER_HANDLER> reader_handler,
        std::shared_ptr<FAILOVER_CONNECTION_HANDLER> connection_handler,
        int writer_failover_timeout_ms, int read_topology_interval_ms,
        int reconnect_writer_interval_ms);
    ~FAILOVER_WRITER_HANDLER();
    WRITER_FAILOVER_RESULT failover(
        std::shared_ptr<CLUSTER_TOPOLOGY_INFO> current_topology);

   protected:
    int read_topology_interval_ms = 5000;     // 5 sec
    int reconnect_writer_interval_ms = 5000;  // 5 sec
    int writer_failover_timeout_ms = 60000;   // 60 sec

   private:
    std::shared_ptr<TOPOLOGY_SERVICE> topology_service;
    std::shared_ptr<FAILOVER_CONNECTION_HANDLER> connection_handler;
    std::shared_ptr<FAILOVER_READER_HANDLER> reader_handler;
};

class FAILOVER_HANDLER {
   private:
    DBC* dbc = nullptr;
    TOPOLOGY_SERVICE* topology_service;
    FAILOVER_READER_HANDLER* failover_reader_handler;
    FAILOVER_WRITER_HANDLER* failover_writer_handler;
    std::vector<HOST_INFO*>* hosts = nullptr;  // topology  - TODO not needed?
    HOST_INFO* current_host = nullptr;
    FAILOVER_CONNECTION_HANDLER* connection_handler = nullptr;

    bool is_cluster_topology_available = false;
    bool is_multi_writer_cluster = false;
    bool is_rds_proxy = false;
    bool is_rds = false;
    bool is_rds_custom_cluster = false;

    void init_cluster_info();
    bool is_failover_enabled();
    bool is_dns_pattern_valid(std::string host);
    bool is_rds_dns(std::string host);
    bool is_rds_proxy_dns(std::string host);
    bool is_rds_custom_cluster_dns(std::string host);
    void create_connection_and_initialize_topology();
    std::string get_rds_cluster_host_url(std::string host);
    std::string get_rds_instance_host_pattern(std::string host);
    bool is_ipv4(std::string host);
    bool is_ipv6(std::string host);
    bool failover_to_reader(const char*& new_error_code);
    bool failover_to_writer(const char*& new_error_code);
    void refresh_topology();

   public:
    FAILOVER_HANDLER(DBC* dbc, TOPOLOGY_SERVICE* topology_service);
    bool trigger_failover_if_needed(const char* error_code,
                                    const char*& new_error_code);
    ~FAILOVER_HANDLER();
};

// ************************************************************************************************
// These are failover utilities/helpers. Perhaps belong to a separate header
// file, but here for now
//

// FAILOVER_SYNC enables synchronization between threads
class FAILOVER_SYNC {
   public:
    FAILOVER_SYNC();
    void mark_as_done();
    void wait_for_done();
    void wait_for_done(int milliseconds);

   private:
    bool done_;
    std::mutex mutex_;
    std::condition_variable cv;
};

class FAILOVER {
   public:
    FAILOVER(std::shared_ptr<FAILOVER_CONNECTION_HANDLER> connection_handler,
             std::shared_ptr<TOPOLOGY_SERVICE> topology_service);
    virtual ~FAILOVER();
    void cancel();
    bool is_canceled();
    bool is_writer_connected();
    std::shared_ptr<MYSQL> get_connection();

   protected:
    bool connect(std::shared_ptr<HOST_INFO> host_info);
    void sleep(int miliseconds);
    std::shared_ptr<FAILOVER_CONNECTION_HANDLER> connection_handler;
    std::shared_ptr<TOPOLOGY_SERVICE> topology_service;

   private:
    std::atomic_bool canceled;
    // TODO probably wrap this shared MYSQL pointer in some result class so all
    // the methods and interfaces never show it directly
    std::shared_ptr<MYSQL> new_connection;

    void close_connection();
};

class RECONNECT_TO_WRITER_HANDLER : public FAILOVER {
   public:
    RECONNECT_TO_WRITER_HANDLER(
        std::shared_ptr<FAILOVER_CONNECTION_HANDLER> connection_handler,
        std::shared_ptr<TOPOLOGY_SERVICE> topology_servicets,
        int connection_interval);
    ~RECONNECT_TO_WRITER_HANDLER();

    WRITER_FAILOVER_RESULT operator()(
        const std::shared_ptr<HOST_INFO>& original_writer,
        FAILOVER_SYNC& f_sync);

   private:
    int reconnect_interval_ms;

    bool is_current_host_writer(
        const std::shared_ptr<HOST_INFO>& original_writer,
        const std::shared_ptr<CLUSTER_TOPOLOGY_INFO>& latest_topology);
};

class WAIT_NEW_WRITER_HANDLER : public FAILOVER {
   public:
    WAIT_NEW_WRITER_HANDLER(
        std::shared_ptr<FAILOVER_CONNECTION_HANDLER> connection_handler,
        std::shared_ptr<TOPOLOGY_SERVICE> topology_service,
        std::shared_ptr<CLUSTER_TOPOLOGY_INFO> current_topology,
        std::shared_ptr<FAILOVER_READER_HANDLER> reader_handler,
        int connection_interval);
    ~WAIT_NEW_WRITER_HANDLER();

    WRITER_FAILOVER_RESULT operator()(
        const std::shared_ptr<HOST_INFO>& original_writer,
        FAILOVER_SYNC& f_sync);

   private:
    // TODO - initialize in constructor and define constant for default value
    int read_topology_interval_ms = 5000;
    std::shared_ptr<FAILOVER_READER_HANDLER> reader_handler;
    std::shared_ptr<CLUSTER_TOPOLOGY_INFO> current_topology;
    std::shared_ptr<MYSQL> reader_connection;  // To retrieve latest topology
    std::shared_ptr<MYSQL> current_connection;
    std::shared_ptr<HOST_INFO> current_reader_host;

    void refresh_topology_and_connect_to_new_writer(
        const std::shared_ptr<HOST_INFO>& original_writer);
    void connect_to_reader();
    bool connect_to_writer(const std::shared_ptr<HOST_INFO>& writer_candidate);
    void clean_up_reader_connection();
};

#endif /* __FAILOVER_H__ */
