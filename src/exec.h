#pragma once

#include "resp_serde.h"
namespace redispp {
namespace exec {
struct AppendCmd {
  resp::String key;
  resp::String val;

  static constexpr std::string_view Name = "APPEND";
};

struct DecrCmd {
  resp::String key;

  static constexpr std::string_view Name = "DECR";
};

struct DecrByCmd {
  resp::String key;
  resp::Integer val;

  static constexpr std::string_view Name = "DECRBY";
};

struct GetCmd {
  resp::String key;

  static constexpr std::string_view Name = "GET";
};

struct GetDelCmd {
  resp::String key;

  static constexpr std::string_view Name = "GETDEL";
};

struct GetRangeCmd {
  resp::String key;
  size_t start;
  size_t end;

  static constexpr std::string_view Name = "GETRANGE";
};

struct GetSetCmd {
  resp::String key;
  resp::String val;

  static constexpr std::string_view Name = "GETSET";
};

struct IncrCmd {
  resp::String key;

  static constexpr std::string_view Name = "INCR";
};

struct IncrByCmd {
  resp::String key;
  resp::Integer val;

  static constexpr std::string_view Name = "INCRBY";
};

struct SetCmd {
  resp::String key;
  resp::String val;

  static constexpr std::string_view Name = "SET";
};

struct SetRangeCmd {
  resp::String key;
  size_t offset;
  resp::String val;

  static constexpr std::string_view Name = "SETRANGE";
};

struct StrLenCmd {
  resp::String key;

  static constexpr std::string_view Name = "STRLEN";
};

using Command = std::variant<AppendCmd,
                             DecrCmd,
                             DecrByCmd,
                             GetCmd,
                             GetDelCmd,
                             GetRangeCmd,
                             GetSetCmd,
                             IncrCmd,
                             IncrByCmd,
                             SetCmd,
                             SetRangeCmd,
                             StrLenCmd>;
}  // namespace exec
class DB;
class Client;

auto Execute(DB &db,
             Client &client,
             resp::Deserializer &query_reader,
             resp::Serializer &resp_sender,
             redispp::resp::Channel &ch) -> boost::asio::awaitable<void>;
}  // namespace redispp