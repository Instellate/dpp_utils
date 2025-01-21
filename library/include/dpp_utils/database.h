#pragma once

#ifdef DPP_EXPORT_PG

#include <dpp/appcommand.h>
#include <dpp/cluster.h>

#include <libpq-fe.h>

#include <mutex>
#include <optional>
#include <random>
#include <unordered_map>

namespace dpp_utils {

class row {
    std::shared_ptr<PGresult> _result;
    int _row_index;

    explicit row(std::shared_ptr<PGresult> result, int row_index);

    friend struct row_iterator;
    friend class result;

  public:
    using value_variant = std::variant<std::monostate, int64_t, bool, double,
                                       int32_t, float, int16_t, std::string>;

    value_variant get(const std::string &column_name) const;

    value_variant get(int column_index) const;

    template <typename T> T get(const std::string &column_name) const {
        return std::get<T>(get(column_name));
    }

    template <typename T> T get(int column_index) const {
        return std::get<T>(get(column_index));
    }
};

struct row_iterator {
    using difference_type = std::ptrdiff_t;
    using value_type = row;

    const row &operator*() const;

    row_iterator &operator++();

    row_iterator operator++(int);

    bool operator==(const row_iterator &it) const;

    row_iterator();

    row_iterator(const row_iterator &) = default;
    row_iterator(row_iterator &&) noexcept = default;
    row_iterator &operator=(const row_iterator &) = default;
    row_iterator &operator=(row_iterator &&) noexcept = default;

  private:
    friend class result;
    explicit row_iterator(row row);

    std::optional<row> _row;
};

static_assert(std::forward_iterator<row_iterator>);

class result {
    std::shared_ptr<PGresult> _result;
    std::string _error_message{};

    explicit result(PGresult *result);

    friend class database;

  public:
    result(result &&) = default;
    result(const result &) = default;

    result &operator=(result &&) = default;
    result &operator=(const result &) = default;

    row_iterator begin() const;
    row_iterator end() const;

    std::string error() const;

    row operator[](int index) const;
};

template <typename> struct to_param_string;

template <> struct to_param_string<std::string> {
    static std::string to_string(std::string &&val) { return val; }
};

template <> struct to_param_string<int32_t> {
    static std::string to_string(int32_t val) { return std::to_string(val); }
};

template <> struct to_param_string<int64_t> {
    static std::string to_string(int64_t val) { return std::to_string(val); }
};

template <> struct to_param_string<int16_t> {
    static std::string to_string(int16_t val) { return std::to_string(val); }
};

template <> struct to_param_string<float> {
    static std::string to_string(float val) { return std::to_string(val); }
};

template <> struct to_param_string<double> {
    static std::string to_string(double val) { return std::to_string(val); }
};

template <> struct to_param_string<bool> {
    static std::string to_string(bool val) {
        if (val) {
            return "true";
        } else {
            return "false";
        }
    }
};

template <typename, typename = std::void_t<>>
struct is_param_string_convertible : std::false_type {};

template <typename T>
struct is_param_string_convertible<
    T, std::void_t<
           std::is_same<std::string, decltype(to_param_string<T>::to_string(
                                         std::declval<T>()))>>>
    : std::true_type {};

class database {
    const char *_psuedo_chars =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

    PGconn *_conn;
    std::mutex _m{};
    std::unordered_map<std::string, std::string> _prepared_map{};

    std::random_device _rand_device{};
    std::default_random_engine _rand_engine{_rand_device()};
    std::uniform_int_distribution<int> _rand_dist{
        0, static_cast<int>(strlen(_psuedo_chars) - 1)};

  public:
    using query_callback = std::function<void(const result &)>;
    using param_strings = std::vector<std::optional<std::string>>;

    explicit database(const char *connection_string);

    ~database();

    database(const database &) = delete;
    database(database &&) = delete;

    database &operator=(const database &) = delete;
    database &operator=(database &&) = delete;

    void start(const dpp::cluster &cluster);

    void query(const std::string &stmnt, const query_callback &cb,
               param_strings &&args);

#ifdef DPP_CORO
    dpp::async<result> co_query(const std::string &stmnt, param_strings &&vec);
#endif

    void prepare(const std::string &stmnt, const query_callback &cb,
                 int params_count);

  private:
    template <typename... Args> param_strings get_param_strings(Args... args) {
        param_strings strings;
        return get_param_strings(std::move(strings),
                                 std::forward<Args>(args)...);
    }

    template <typename... Args>
    param_strings get_param_strings(param_strings &&strings, Args... args) {
        return strings;
    }

    template <typename T, typename... Args>
    param_strings get_param_strings(param_strings &&strings, T value,
                                    Args... args) {
        using NoCVRefT = std::remove_cvref_t<T>;
        static_assert(is_param_string_convertible<NoCVRefT>::value);

        if constexpr (std::is_move_constructible_v<NoCVRefT>) {
            strings.emplace_back(
                to_param_string<NoCVRefT>::to_string(std::move(value)));
        } else {
            strings.emplace_back(to_param_string<NoCVRefT>::to_string(value));
        }

        return get_param_strings(std::move(strings),
                                 std::forward<Args>(args)...);
    }

    template <typename T, typename... Args>
    param_strings get_param_strings(param_strings &&strings,
                                    std::optional<T> value, Args... args) {
        using NoCVRefT = std::remove_cvref_t<T>;
        static_assert(is_param_string_convertible<NoCVRefT>::value);

        if (value.has_value()) {
            if constexpr (std::is_move_constructible_v<NoCVRefT>) {
                strings.emplace_back(to_param_string<NoCVRefT>::to_string(
                    std::move(value).value()));
            } else {
                strings.emplace_back(
                    to_param_string<NoCVRefT>::to_string(value.value()));
            }
        } else {
            strings.emplace_back(std::nullopt);
        }

        return get_param_strings(std::move(strings),
                                 std::forward<Args>(args)...);
    }

    void on_read(dpp::socket fd, const dpp::socket_events &e);

    void process_result(PGresult *result);

    std::string generate_random_str();

    std::deque<query_callback> _callbacks{};

  public:
    template <typename... Args>
    void query(const std::string &stmnt, const query_callback &cb,
               Args... args) {
        param_strings strings = get_param_strings(std::forward<Args>(args)...);
        query(stmnt, cb, std::move(strings));
    }

#ifdef DPP_CORO
    template <typename... Args>
    dpp::async<result> co_query(const std::string &stmnt, Args... args) {
        param_strings strings = get_param_strings(std::forward<Args>(args)...);
        return co_query(stmnt, std::move(strings));
    }
#endif
};

} // namespace dpp_utils

#endif