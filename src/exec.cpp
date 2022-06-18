#include "exec.h"

#include <charconv>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>

#include "message.h"

namespace redispp {
auto to_int(std::string_view str) -> std::optional<Integer> {
  Integer i;
  auto res = std::from_chars(str.begin(), str.end(), i);

  if (res.ec == std::errc{}) {
    return {};
  }
  return i;
}

void to_str(std::pmr::string &str, Integer i) {
  str.clear();
  fmt::format_to(std::back_inserter(str), "{}", i);
}

auto Execute(DB & /*db*/, Client & /*client*/, Message /*query*/) -> Message { return {}; }
}  // namespace redispp