/**
  @file  failover_writer_handler.c
  @brief Failover functions.
*/

#include "driver.h"

#include <thread>
#include <chrono>

// **** FAILOVER_SYNC ***************************************
//used for thread syncronization
FAILOVER_SYNC::FAILOVER_SYNC() : done_{ false } {}

void FAILOVER_SYNC::mark_as_done() {
    // TODO implement
}

void FAILOVER_SYNC::wait_for_done() {
    // TODO implement
}

void FAILOVER_SYNC::wait_for_done(int milliseconds) {
    // TODO implement
}

bool FAILOVER_SYNC::is_done() {
    return done_;
}

// ************* FAILOVER ***********************************
// Base class of two writer failover task handlers
FAILOVER::FAILOVER(std::shared_ptr<FAILOVER_CONNECTION_HANDLER> ch) 
    : conn_handler{ ch }, new_conn{ nullptr }, canceled{ false } {
}
FAILOVER::~FAILOVER() {
    close_connection();
}

void FAILOVER::cancel() { 
    canceled = true; 
}

bool FAILOVER::is_canceled() { 
    return canceled;
}

bool FAILOVER::connected() {
    return new_conn != nullptr;
}

bool FAILOVER::connect(std::shared_ptr<HOST_INFO> host_info) {
    new_conn = std::make_shared<MYSQL>(*conn_handler->connect(host_info.get()));
    return new_conn != nullptr;
}

std::shared_ptr<MYSQL> FAILOVER::get_connection() {
    return new_conn;
}

void FAILOVER::close_connection() {
    mysql_close(new_conn.get());
}

void FAILOVER::sleep(int miliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(miliseconds));
}

// ************************ FAILOVER_RECONNECT_HANDLER
//handler reconnecting to a given host, e.g. reconnect to a current writer
FAILOVER_RECONNECT_HANDLER::FAILOVER_RECONNECT_HANDLER(std::shared_ptr<FAILOVER_CONNECTION_HANDLER> ch, int conn_interval) 
    : FAILOVER{ ch}, reconnect_interval_ms{ conn_interval } {
}

FAILOVER_RECONNECT_HANDLER::~FAILOVER_RECONNECT_HANDLER() {
}

void FAILOVER_RECONNECT_HANDLER::operator()(std::shared_ptr<HOST_INFO> hi, FAILOVER_SYNC& f_sync) {  
    if (hi) {
        while (!is_canceled() && !connect(hi)) {
            sleep(reconnect_interval_ms);
        }
    }
    f_sync.mark_as_done();
}

// ************************ WAIT_NEW_WRITER_HANDLER

WAIT_NEW_WRITER_HANDLER::WAIT_NEW_WRITER_HANDLER(std::shared_ptr<FAILOVER_CONNECTION_HANDLER> ch, std::shared_ptr<TOPOLOGY_SERVICE> topology_service, FAILOVER_READER_HANDLER& failover_reader_handler, int conn_interval)
    : FAILOVER{ ch }, topology_service{ topology_service }, reader_handler{ failover_reader_handler }, read_topology_interval_ms{ conn_interval } {
}

WAIT_NEW_WRITER_HANDLER::~WAIT_NEW_WRITER_HANDLER() {
    // TODO
}

void WAIT_NEW_WRITER_HANDLER::operator()(FAILOVER_SYNC& f_sync) {   
    // TODO implement
}

std::shared_ptr<MYSQL> WAIT_NEW_WRITER_HANDLER::get_reader_connection() {
    // TODO implement
    return NULL;
}

bool WAIT_NEW_WRITER_HANDLER::refreshTopologyAndConnectToNewWriter(std::shared_ptr<MYSQL> reader_connection) {
    // TODO implement
    return false;
}

// ****************FAILOVER_WRITER_HANDLER  ************************************
FAILOVER_WRITER_HANDLER::FAILOVER_WRITER_HANDLER(TOPOLOGY_SERVICE* topology_service, FAILOVER_READER_HANDLER* failover_reader_handler)
    : read_topology_interval_ms{ 5000 } // 5 sec
{}

FAILOVER_WRITER_HANDLER::~FAILOVER_WRITER_HANDLER() {}

bool FAILOVER_WRITER_HANDLER::is_host_same(HOST_INFO* h1, HOST_INFO* h2) {
    // TODO implement
    return false;
}

void FAILOVER_WRITER_HANDLER::sleep(int miliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(miliseconds));
}

// see how this is used, but potentially set the TOPOLOGY_SERVICE* ts, FAILOVER_CONNECTION_HANDLER* conn_handler
// in constructor and use memeber variables
WRITER_FAILOVER_RESULT FAILOVER_WRITER_HANDLER::failover(TOPOLOGY_SERVICE* topology_service, FAILOVER_CONNECTION_HANDLER* conn_handler, FAILOVER_READER_HANDLER& failover_reader_handler)
{
    // TODO implement
    return WRITER_FAILOVER_RESULT{};
}
