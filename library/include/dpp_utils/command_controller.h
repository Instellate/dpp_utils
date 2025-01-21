#pragma once

#include <dpp/dispatcher.h>

#include <optional>
#include <type_traits>

namespace dpp_utils {

template <typename T> class command_controller_base {
  public:
    virtual ~command_controller_base() = default;

  private:
    struct register_methods {
        register_methods() { T::init_commands(); }
    };

    static register_methods _methods;
    virtual void *touch() { return &_methods; }
};

namespace internal {

template <typename> struct function_info;

template <typename ReturnType, typename... Args>
struct function_info<ReturnType(Args...)> {
    using arguments = std::tuple<Args...>;
    static constexpr size_t arguments_count = sizeof...(Args);

    template <size_t I> using nth_argument = std::tuple_element<I, arguments>;
};

struct command_executor_base {
    virtual void execute_command(const dpp::slashcommand_t &event) = 0;
};

template <typename> struct is_optional : std::false_type {};

template <typename T> struct is_optional<std::optional<T>> : std::true_type {};

template <typename Function>
struct command_executor final : public command_executor_base {
    Function &_function;
    std::vector<std::string> _options;

    explicit command_executor(Function &function,
                              std::vector<std::string> &&options)
        : _function(function), _options(std::move(options)) {}

    command_executor() = delete;
    command_executor(command_executor &&) = delete;
    command_executor(const command_executor &) = delete;

    using info = function_info<Function>;

    void execute_command(const dpp::slashcommand_t &event) override {}

  private:
    template <typename... Args>
    void process(const dpp::slashcommand_t &event, size_t args_offset,
                 Args... args) {
        constexpr size_t arg_count = sizeof...(Args);

        if constexpr (arg_count < info::arguments_count) {
            using current_arg = std::remove_const_t<std::remove_cvref_t<
                typename info::template nth_argument<arg_count>>>;

            if constexpr (is_optional<current_arg>::value) {
                using optional_type = typename current_arg::value_type;

                const auto &param =
                    event.get_parameter(this->_options.get(arg_count));
                optional_type *type = std::get_if<optional_type>(&param);
                if (type == nullptr) {
                    return process(event, args_offset,
                                   std::forward<Args>(args)..., std::nullopt);
                } else {
                    return process(event, args_offset,
                                   std::forward<Args>(args)..., *type);
                }
            } else if constexpr (std::is_same_v<current_arg,
                                                dpp::slashcommand_t>) {
                return process(event, args_offset, std::forward<Args>(args)...,
                               event);
            } else {
                current_arg arg = std::get<current_arg>(
                    event.get_parameter(this->_options.get(arg_count)));
                return process(event, args_offset, std::forward<Args>(args)...,
                               arg);
            }
        } else if constexpr (arg_count == info::arguments_count) {
            return this->_function(std::forward<Args>(args)...);
        } else {
            throw std::runtime_error{"Impossible condition"};
        }
    }
};

} // namespace internal

} // namespace dpp_utils