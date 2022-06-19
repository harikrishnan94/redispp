#pragma once

#include <string>
#include <string_view>

namespace redispp::utils {
struct string_hash {
  using is_transparent = void;
  [[nodiscard]] auto operator()(const char *txt) const -> size_t { return std::hash<std::string_view>{}(txt); }
  [[nodiscard]] auto operator()(std::string_view txt) const -> size_t { return std::hash<std::string_view>{}(txt); }
  [[nodiscard]] auto operator()(const std::pmr::string &txt) const -> size_t {
    return std::hash<std::pmr::string>{}(txt);
  }
};
}  // namespace redispp::utils