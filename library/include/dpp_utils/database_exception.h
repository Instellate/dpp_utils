#pragma once

#ifdef DPP_EXPORT_PG

#include <exception>
#include <string>

namespace dpp_utils {
class database_exception final : std::exception {
    std::string _msg;

  public:
    explicit database_exception(const std::string &msg) : _msg(msg) {}

    explicit database_exception(std::string &&msg) : _msg(std::move(msg)) {}

    const char *what() const noexcept override { return _msg.c_str(); }
};

} // namespace dpp_utils

#endif