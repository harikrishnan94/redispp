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
  resp::Integer start;
  resp::Integer end;

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
                             StrLenCmd>;
}  // namespace exec

class DB;
class Client;

class Response {
 public:
  explicit Response(bool is_array = false) : m_is_array(is_array) {}

  Response(resp::Token tok) { m_tokens.push_back(std::move(tok)); }  // NOLINT(hicpp-explicit-conversions)

  void Push(resp::Token tok);
  auto Serialize(resp::Serializer &resp_sender) const -> boost::asio::awaitable<void>;

 private:
  std::vector<resp::Token> m_tokens;
  bool m_is_array = false;
};

auto Execute(DB &db, Client &client, resp::Deserializer &query_reader) -> boost::asio::awaitable<Response>;
}  // namespace redispp