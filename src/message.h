#pragma once

#include <memory_resource>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace redispp {
enum class TokenTypeMarker : char { SimpleString = '+', Error = '-', Integer = ':', BulkString = '$', Array = '*' };

using Integer = std::int64_t;
using String = std::pmr::string;
using String = std::optional<String>;
struct ErrorMessage {
  String msg;
};
struct InlineMessage {
  std::pmr::string msg_str;
  std::pmr::vector<std::string_view> parts;
};
using SingularMessage = std::variant<Integer, String, String, ErrorMessage>;
using MessageArray = std::optional<std::pmr::vector<SingularMessage>>;
using Message = std::variant<SingularMessage, InlineMessage, MessageArray>;

static constexpr std::string_view MessagePartTerminator = "\r\n";

}  // namespace redispp