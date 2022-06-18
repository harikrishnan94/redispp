#pragma once

#include <memory_resource>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace redispp {
enum class MessageTypeMarker : char { SimpleString = '+', Error = '-', Integer = ':', BulkString = '$', Array = '*' };

using Integer = std::int64_t;
using Str = std::pmr::string;
using String = std::optional<Str>;
struct ErrorMessage {
  Str msg;
};
using SingularMessage = std::variant<Integer, Str, String, ErrorMessage>;
using MessageArray = std::optional<std::pmr::vector<SingularMessage>>;
using Message = std::variant<SingularMessage, MessageArray>;

static constexpr std::string_view MessagePartTerminator = "\r\n";

}  // namespace redispp