#pragma once

#include <dpp/dispatcher.h>

#include <optional>
#include <type_traits>

namespace dpp_utils {

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

    void execute_command(const dpp::slashcommand_t &event) override {
        process(event, 0);
    }

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

struct injectable_base {
    virtual ~injectable_base() = 0;
};

using injectable_base_ptr = std::unique_ptr<injectable_base>;
using injectable_constructor =
    std::function<injectable_base_ptr &(const service_provider_ptr &)>;

class service_provider final {
    std::unordered_map<size_t, injectable_constructor> _injectables;

    service_provider(
        std::unordered_map<size_t, injectable_constructor> &&injectables)
        : _injectables(std::move(injectables)) {}

  public:
    class builder {
        std::unordered_map<size_t, injectable_constructor> _injectables;

      public:
        builder() = default;

        template <typename T> builder &add_singleton_service() {
            this->_injectables.emplace(
                typeid(T).hash_code(),
                [](const service_provider_ptr &provider) {
                    static std::optional<std::unique_ptr<T>> value;
                    if (value.has_value()) {
                        return value.value();
                    } else {
                        value =
                            std::make_unique<T>(T::create_instance(provider));
                        return value.value();
                    }
                });

            return *this;
        }
    };

    service_provider(const service_provider &) = delete;
    service_provider(service_provider &&) = delete;

    service_provider &operator=(const service_provider &) = delete;
    service_provider &operator=(service_provider &&) = delete;

    template <typename T> std::unique_ptr<T> &get_service() const {
        auto it = this->_injectables.find(typeid(T).hash_code());
        if (it == this->_injectables.end()) {
            return nullptr;
        }

        auto &ptr = it->second;
        return reinterpret_cast<std::unique_ptr<T> &>(ptr);
    }

    template <typename T> std::unique_ptr<T> &et_required_service() const {
        auto it = this->_injectables.find(typeid(T).hash_code());
        if (it == this->_injectables.end()) {
            throw std::invalid_argument{"Couldn't find type specified"};
        }

        auto &ptr = it->second;
        return reinterpret_cast<std::unique_ptr<T> &>(ptr);
    }

  private:
};

using service_provider_ptr = std::shared_ptr<service_provider>;

template <typename T> class injectable : public injectable_base {
  private:
    template <typename... Args>
    static T create_instance_templ(const service_provider_ptr &provider,
                                   Args... args) {
        using info =
            internal::function_info<T>; // Getting constructor arguments

        constexpr size_t arg_count = sizeof...(Args);
        if constexpr (arg_count < info::arguments_count) {

        } else if constexpr (arg_count == info::arguments_count) {

        } else {
            throw std::runtime_error{"Impossible condition"};
        }
    }

  public:
    static T create_instance(const service_provider_ptr &provider) {
        return create_instance_templ(provider);
    }
};

template <typename T> class command_controller_base : public injectable<T> {
  public:
    virtual ~command_controller_base() = default;

  private:
    struct register_methods {
        register_methods() { T::init_commands(); }
    };

    static register_methods _methods;
    virtual void *touch() { return &_methods; }
};

} // namespace dpp_utils