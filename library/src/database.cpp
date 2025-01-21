#include "database.h"

#include <iostream>

#include "database_exception.h"

#include <dpp/appcommand.h>
#include <postgresql/server/catalog/pg_type_d.h>

struct result_deleter {
    void operator()(PGresult *result) const { PQclear(result); }
};

namespace dpp_utils {

row::row(std::shared_ptr<PGresult> result, int row_index)
    : _result(std::move(result)), _row_index(row_index) {}

row::value_variant row::get(const std::string &column_name) const {
    const int column_index =
        PQfnumber(this->_result.get(), column_name.c_str());
    if (column_index == -1) {
        throw std::invalid_argument{"The column name provided did not exist"};
    }

    return get(column_index);
}

row::value_variant row::get(const int column_index) const {
    int field_amount = PQnfields(this->_result.get());
    if (column_index >= field_amount) {
        throw std::out_of_range{"Column index is out of range"};
    }

    if (PQgetisnull(this->_result.get(), this->_row_index, column_index)) {
        return std::monostate{};
    }

    const char *val =
        PQgetvalue(this->_result.get(), this->_row_index, column_index);

    int64_t int8_val = 0;
    double float8_val = 0;
    int32_t int4_val = 0;
    float float4_val = 0;
    int16_t int2_val = 0;
    Oid oid = PQftype(this->_result.get(), column_index);
    switch (oid) {
    case INT8OID:
        return std::stol(val);

    case BOOLOID:
        if (std::strcmp(val, "true")) {
            return true;
        } else {
            return false;
        }

    case FLOAT8OID:
        return std::stod(val);

    case INT4OID:
        return std::stoi(val);

    case FLOAT4OID:
        return std::stof(val);

    case INT2OID:
        return static_cast<int16_t>(std::stoi(val));

    case TEXTOID:
    case VARCHAROID:
    case CHAROID:
        return std::string{val};

    default:
        throw std::range_error{
            "The OID on the given table is not yet implemented"};
    }
}

result::result(PGresult *result) {
    this->_result = std::shared_ptr<PGresult>(result, result_deleter());
}

row_iterator result::begin() const {
    return row_iterator(row(this->_result, 0));
}

row_iterator result::end() const {
    return row_iterator(row(this->_result, PQntuples(this->_result.get())));
}

std::string result::error() const {
    return this->_error_message.empty()
               ? PQresultErrorMessage(this->_result.get())
               : this->_error_message;
}

row result::operator[](const int index) const {
    return row(this->_result, index);
}

database::database(const char *connection_string) {
    this->_conn = PQconnectdb(connection_string);
    if (PQstatus(this->_conn) != CONNECTION_OK) {
        std::string msg = PQerrorMessage(this->_conn);
        throw database_exception(std::move(msg));
    }
}

database::~database() { PQfinish(this->_conn); }

void database::start(const dpp::cluster &cluster) {
    auto &engine = cluster.socketengine;

    const dpp::socket_events events{
        PQsocket(this->_conn), dpp::WANT_READ,
        [this](const dpp::socket fd, const struct dpp::socket_events &e) {
            this->on_read(fd, e);
        }};
    engine->register_socket(events);
}

void database::query(const std::string &stmnt, const query_callback &cb,
                     param_strings &&args) {
    std::unique_lock lock{this->_m};
    if (!this->_prepared_map.contains(stmnt)) {
        lock.unlock();
        int args_size = args.size();
        prepare(
            stmnt,
            [this, stmnt, cb, args = std::move(args)](const result &res) {
                if (!res.error().empty()) {
                    cb(res);
                    return;
                }

                query(stmnt, cb, std::move(args));
            },
            args_size);

        return;
    }

    const auto &name = this->_prepared_map[stmnt];
    const char **arr = static_cast<const char **>(malloc(args.size()));

    for (int i = 0; i < args.size(); ++i) {
        if (args[i].has_value()) {
            arr[i] = args[i]->c_str();
        } else {
            arr[i] = nullptr;
        }
    }

    int i = PQsendQueryPrepared(this->_conn, name.c_str(),
                                static_cast<int>(args.size()), arr, nullptr,
                                nullptr, 0);
    free(arr);

    if (i == 0) {
        std::cerr << PQerrorMessage(this->_conn);
        return;
    }

    this->_callbacks.emplace_back(cb);
}

#if DPP_CORO
dpp::async<result> database::co_query(const std::string &stmnt,
                                      param_strings &&vec) {
    return dpp::async<result>{
        [this, stmnt, vec = std::move(vec)]<typename C>(C &&cc) {
            return query(stmnt, cc, std::move(vec));
        }};
}
#endif

void database::prepare(const std::string &stmnt, const query_callback &cb,
                       int params_count) {
    std::string random_string = this->generate_random_str();

    int i = PQsendPrepare(this->_conn, random_string.c_str(), stmnt.c_str(),
                          params_count, nullptr);
    if (i == 0) {
        std::cerr << PQerrorMessage(this->_conn) << '\n';
        return;
    }

    std::lock_guard lock{this->_m};
    this->_callbacks.emplace_back(
        [this, cb, name = std::move(random_string), stmnt](const result &res) {
            if (!res.error().empty()) {
                cb(res);
                return;
            }

            this->_m.lock();
            this->_prepared_map.emplace(stmnt, std::move(name));
            this->_m.unlock();
            cb(res);
        });
}

void database::on_read(dpp::socket fd, const struct dpp::socket_events &e) {
    if (PQconsumeInput(this->_conn) == 0) {
        std::cerr << "Got error when consuming input: "
                  << PQerrorMessage(this->_conn) << '\n';
        return;
    }

    if (PQisBusy(this->_conn)) {
        return;
    }

    std::vector<PGresult *> results;

    PGresult *raw_result = PQgetResult(this->_conn);
    while (raw_result != nullptr) {
        results.emplace_back(raw_result);
        raw_result = PQgetResult(this->_conn);
    }

    int elements_consumed = this->_callbacks.size();
    for (PGresult *res : results) {
        if (elements_consumed > 0) {
            process_result(res);
            --elements_consumed;
        }
    }
}

void database::process_result(PGresult *result) {
    ExecStatusType type = PQresultStatus(result);

    ::dpp_utils::result res{result};

    std::unique_lock lock{this->_m};
    if (this->_callbacks.empty()) {
        return;
    }

    query_callback callback = this->_callbacks.front();
    this->_callbacks.pop_front();

    lock.unlock();
    callback(res);
}

#define RANDOM_STR_LEN 4

std::string database::generate_random_str() {
    std::string str;
    str.reserve(RANDOM_STR_LEN);

    for (int i = 0; i < RANDOM_STR_LEN; ++i) {
        int random_num = this->_rand_dist(this->_rand_engine);
        str += this->_psuedo_chars[random_num];
    }

    return str;
}

const row &row_iterator::operator*() const { return this->_row.value(); }

row_iterator &row_iterator::operator++() {
    ++this->_row->_row_index;
    return *this;
}

row_iterator row_iterator::operator++(int) {
    row_iterator new_it = *this;
    ++this->_row->_row_index;
    return new_it;
}

bool row_iterator::operator==(const row_iterator &it) const {
    if (!this->_row.has_value() && !it._row.has_value()) {
        return true;
    }

    if (!this->_row.has_value() && it._row.has_value()) {
        return false;
    }

    if (this->_row.has_value() && !it._row.has_value()) {
        return false;
    }

    if (this->_row->_row_index == it._row->_row_index) {
        return true;
    }

    return false;
}

row_iterator::row_iterator() {}

row_iterator::row_iterator(dpp_utils::row row) { this->_row = row; }

} // namespace dpp_utils
