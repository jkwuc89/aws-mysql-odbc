/**
  @file  failover_connection_handler.c
  @brief Failover connection functions.
*/

#include "driver.h"
#include "failover.h"

#include <codecvt>
#include <locale>

#ifdef __linux__
    sqlwchar_string to_sqlwchar_string(const std::string& src) {
        return std::wstring_convert< std::codecvt_utf8_utf16< char16_t >,
                                     char16_t >{}
            .from_bytes(src);
    }
#else
    sqlwchar_string to_sqlwchar_string(const std::string& src) {
        return std::wstring_convert< std::codecvt_utf8< wchar_t >, wchar_t >{}
            .from_bytes(src);
    }
#endif

FAILOVER_CONNECTION_HANDLER::FAILOVER_CONNECTION_HANDLER(DBC* dbc) : dbc{dbc} {}

FAILOVER_CONNECTION_HANDLER::~FAILOVER_CONNECTION_HANDLER() {}

SQLRETURN FAILOVER_CONNECTION_HANDLER::do_connect(DBC* dbc, DataSource* ds) {
    return dbc->connect(ds);
}

std::shared_ptr<CONNECTION_INTERFACE> FAILOVER_CONNECTION_HANDLER::connect(
    std::shared_ptr<HOST_INFO> host_info) {

    if (dbc == nullptr || dbc->ds == nullptr || host_info == nullptr) {
        return nullptr;
    }
    // TODO Convert string to wstring. Note: need to revist if support Linux
    auto host = host_info->get_host();
    auto new_host = to_sqlwchar_string(host);

    DBC* dbc_clone = clone_dbc(dbc);
    ds_set_strnattr(&dbc_clone->ds->server, (SQLWCHAR *) new_host.c_str(), new_host.size());

    std::shared_ptr<CONNECTION_INTERFACE> new_connection;
    CLEAR_DBC_ERROR(dbc_clone);
    SQLRETURN rc = do_connect(dbc_clone, dbc_clone->ds);

    if (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO) {
        new_connection.reset(dbc_clone->mysql);
        dbc_clone->mysql = nullptr;

        // TODO ponder whether or not we should update ds - the data source in
        // original dbc while updating connection. currently as we don't do
        // that, the original ds holds the initial, user supplied connection
        // information, e.g. host, etc. the release_dbc method deletes the
        // cloned dbc and cloned ds.
    }

    // TODO guard these from potential exceptions thrown before, make sure
    // release happens.
    release_dbc(dbc_clone);

    return new_connection;
}

void FAILOVER_CONNECTION_HANDLER::update_connection(
    std::shared_ptr<CONNECTION_INTERFACE> new_connection) {

    if (!new_connection->is_null()) {
        dbc->close();

        // CONNECTION is the only implementation of CONNECTION_INTERFACE
        // so dynamic_cast should be safe here.
        // TODO: Is there an alternative to dynamic_cast here?
        dbc->mysql = dynamic_cast<CONNECTION*>(new_connection.get());
        
        CLEAR_DBC_ERROR(dbc);

        // TODO: should we also update dbc->ds when updating connection? How
        // this would affect the user?  Same mentioned in TODO above
        //  The only difference would be ds->server and ds->server8. Currently
        //  we're presering the initial server information.
    }
}

void FAILOVER_CONNECTION_HANDLER::release_connection(
    std::shared_ptr<CONNECTION_INTERFACE> connection) {

    connection->close_connection();
}

DBC* FAILOVER_CONNECTION_HANDLER::clone_dbc(DBC* source_dbc) {

    DBC* dbc_clone;

    SQLRETURN status = SQL_ERROR;
    if (source_dbc && source_dbc->env) {
        SQLHANDLE hdbc = nullptr;
        status = SQLAllocHandle(SQL_HANDLE_DBC,
                                static_cast<SQLHANDLE>(source_dbc->env), &hdbc);
        if (status == SQL_SUCCESS || status == SQL_SUCCESS_WITH_INFO) {
            dbc_clone = static_cast<DBC*>(hdbc);
            dbc_clone->ds = ds_new();
            ds_copy(dbc_clone->ds, source_dbc->ds);
        } else {
            throw std::runtime_error(
                "Cannot allocate connection handle when cloning DBC in writer "
                "failover process");
        }
    }
    return dbc_clone;
}

void FAILOVER_CONNECTION_HANDLER::release_dbc(DBC* dbc_clone) {
    // dbc->ds is deleted(if not null) in DBC destructor
    // no need to separately clean it.
    SQLFreeHandle(SQL_HANDLE_DBC, static_cast<SQLHANDLE>(dbc_clone));
}
