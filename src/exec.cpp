#include "exec.h"

#include <fmt/format.h>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <charconv>
#include <variant>

#include "db.h"
#include "resp_serde.h"
#include "string_hash.h"

using namespace boost::asio::experimental::awaitable_operators;
using boost::asio::use_awaitable;

namespace redispp {
using namespace resp;
using namespace exec;

class ExecutionException : std::exception {
 public:
  explicit ExecutionException(String error) : m_error(std::move(error)) {}

  [[nodiscard]] auto what() const noexcept -> const char * override { return m_error.c_str(); }

 private:
  String m_error;
};

static auto to_int(std::string_view str) -> std::optional<Integer> {
  Integer i = 0;
  auto res = std::from_chars(str.begin(), str.end(), i);

  if (res.ec == std::errc{}) {
    throw ExecutionException{"CONVERSION_ERROR Invalid Integer"};
  }
  return i;
}

static void to_str(std::pmr::string &str, Integer i) {
  str.clear();
  fmt::format_to(std::back_inserter(str), "{}", i);
}

static auto get_str(Token tok) -> String {
  if (std::holds_alternative<NullStr_t>(tok)) {
    throw ExecutionException{"EMPTY_INPUT Expected NonNull String"};
  }
  if (auto *s = std::get_if<String>(&tok)) {
    return std::move(*s);
  }
  throw ExecutionException{"WRONG_INPUT_TYPE Expected String"};
}

static auto get_int(Token tok) -> Integer {
  if (const auto *i = std::get_if<Integer>(&tok)) {
    return *i;
  }

  auto int_str = get_str(std::move(tok));
  if (auto i = to_int(int_str)) {
    return *i;
  }

  throw ExecutionException{"WRONG_INPUT_TYPE Expected Integer"};
}

template <typename RealCommand>
auto parse(Channel &ch) -> boost::asio::awaitable<Command>;

template <>
auto parse<AppendCmd>(Channel &ch) -> boost::asio::awaitable<Command> {
  AppendCmd append;

  append.key = get_str(co_await ch.async_receive(use_awaitable));
  append.val = get_str(co_await ch.async_receive(use_awaitable));

  co_return append;
}

template <>
auto parse<DecrCmd>(Channel &ch) -> boost::asio::awaitable<Command> {
  DecrCmd decr;

  decr.key = get_str(co_await ch.async_receive(use_awaitable));

  co_return decr;
}

template <>
auto parse<DecrByCmd>(Channel &ch) -> boost::asio::awaitable<Command> {
  DecrByCmd decr;

  decr.key = get_str(co_await ch.async_receive(use_awaitable));
  decr.val = get_int(co_await ch.async_receive(use_awaitable));

  co_return decr;
}

template <>
auto parse<GetCmd>(Channel &ch) -> boost::asio::awaitable<Command> {
  GetCmd get;

  get.key = get_str(co_await ch.async_receive(use_awaitable));

  co_return get;
}

template <>
auto parse<GetDelCmd>(Channel &ch) -> boost::asio::awaitable<Command> {
  GetDelCmd getdel;

  getdel.key = get_str(co_await ch.async_receive(use_awaitable));

  co_return getdel;
}

template <>
auto parse<GetRangeCmd>(Channel &ch) -> boost::asio::awaitable<Command> {
  GetRangeCmd getrange;

  getrange.key = get_str(co_await ch.async_receive(use_awaitable));
  getrange.start = get_int(co_await ch.async_receive(use_awaitable));
  getrange.end = get_int(co_await ch.async_receive(use_awaitable));

  co_return getrange;
}

template <>
auto parse<GetSetCmd>(Channel &ch) -> boost::asio::awaitable<Command> {
  GetSetCmd getset;

  getset.key = get_str(co_await ch.async_receive(use_awaitable));
  getset.val = get_str(co_await ch.async_receive(use_awaitable));

  co_return getset;
}

template <>
auto parse<IncrCmd>(Channel &ch) -> boost::asio::awaitable<Command> {
  IncrCmd incr;

  incr.key = get_str(co_await ch.async_receive(use_awaitable));

  co_return incr;
}

template <>
auto parse<IncrByCmd>(Channel &ch) -> boost::asio::awaitable<Command> {
  IncrByCmd incr;

  incr.key = get_str(co_await ch.async_receive(use_awaitable));
  incr.val = get_int(co_await ch.async_receive(use_awaitable));

  co_return incr;
}

template <>
auto parse<SetCmd>(Channel &ch) -> boost::asio::awaitable<Command> {
  SetCmd set;

  set.key = get_str(co_await ch.async_receive(use_awaitable));
  set.val = get_str(co_await ch.async_receive(use_awaitable));

  co_return set;
}

template <>
auto parse<StrLenCmd>(Channel &ch) -> boost::asio::awaitable<Command> {
  StrLenCmd strlen;

  strlen.key = get_str(co_await ch.async_receive(use_awaitable));

  co_return strlen;
}

using ParseFunc = auto(*)(Channel &ch) -> boost::asio::awaitable<Command>;

static const std::unordered_map<std::string_view, ParseFunc, utils::string_hash, std::equal_to<>> ParseFuncs = {
    {AppendCmd::Name, parse<AppendCmd>},
    {DecrCmd::Name, parse<DecrCmd>},
    {DecrByCmd::Name, parse<DecrByCmd>},
    {GetCmd::Name, parse<GetCmd>},
    {GetDelCmd::Name, parse<GetDelCmd>},
    {GetRangeCmd::Name, parse<GetRangeCmd>},
    {GetSetCmd::Name, parse<GetSetCmd>},
    {IncrCmd::Name, parse<IncrCmd>},
    {IncrByCmd::Name, parse<IncrByCmd>},
    {SetCmd::Name, parse<SetCmd>},
    {StrLenCmd::Name, parse<StrLenCmd>}};

void Response::Push(Token tok) { m_tokens.push_back(std::move(tok)); }

auto Response::Serialize(Serializer &resp_sender) const -> boost::asio::awaitable<void> {
  if (m_is_array) {
    co_await resp_sender.SerializeArrayHeader(m_tokens.size());
  }

  for (const auto &tok : m_tokens) {
    co_await resp_sender.Serialize(tok);
  }
}

static auto execute(DB &db, Client & /*cli*/, AppendCmd append) -> Response {
  if (auto *val = db.Get(append.key)) {
    (*val) += append.val;
    return Token(Integer(val->length()));
  }

  auto len = static_cast<Integer>(append.val.length());
  db.GetAndSet(std::move(append.key), std::move(append.val));
  return Token(len);
}

static auto execute(DB &db, Client & /*cli*/, DecrByCmd decr) -> Response {
  if (auto *val = db.Get(decr.key)) {
    if (auto i = to_int(*val)) {
      *i -= decr.val;
      return Token(*i);
    }

    return Token(Error{"CONVERSION_ERROR"});
  }

  auto str = db.NewString();
  to_str(str, -decr.val);
  db.GetAndSet(std::move(decr.key), str);

  return Token(std::move(str));
}

static auto execute(DB &db, Client &cli, DecrCmd decr) -> Response {
  return execute(db, cli, DecrByCmd{std::move(decr.key), 1});
}

static auto execute(DB &db, Client & /*cli*/, const GetCmd &get) -> Response {
  if (auto *val = db.Get(get.key)) {
    return Token(*val);
  }
  return Token(NullStr);
}

static auto execute(DB &db, Client & /*cli*/, const GetDelCmd &getdel) -> Response {
  if (auto val = db.Delete(getdel.key)) {
    return Token(std::move(*val));
  }

  return Token(NullStr);
}

static auto execute(DB &db, Client & /*cli*/, GetRangeCmd getrange) -> Response {
  if (auto *val = db.Get(getrange.key)) {
    if ((getrange.start < 0 && getrange.end >= 0) || (getrange.end < 0 && getrange.start >= 0)) {
      return Token(Error{"INVALID RANGE"});
    }

    getrange.start = getrange.start < 0 ? Integer(val->length()) - getrange.start : getrange.start;
    getrange.end = getrange.end < 0 ? Integer(val->length()) - getrange.end : getrange.end;
    const auto start = std::min({getrange.start, getrange.end, Integer(val->length())});
    const auto end = std::min({std::max({getrange.start, getrange.end}), Integer(val->length())});

    if (start != Integer(val->length())) {
      return Token(val->substr(start, end + 1 - start));
    }

    return Token("");
  }

  return Token(NullStr);
}

static auto execute(DB &db, Client & /*cli*/, GetSetCmd getset) -> Response {
  if (auto oldval = db.GetAndSet(std::move(getset.key), std::move(getset.val))) {
    return Token(*oldval);
  }
  return Token("");
}

static auto execute(DB &db, Client & /*cli*/, IncrByCmd incr) -> Response {
  if (auto *val = db.Get(incr.key)) {
    if (auto i = to_int(*val)) {
      *i += incr.val;
      return Token(*i);
    }
    return Token(Error{"CONVERSION_ERROR"});
  }
  auto str = db.NewString();
  to_str(str, incr.val);
  db.GetAndSet(std::move(incr.key), str);
  return Token(std::move(str));
}

static auto execute(DB &db, Client &cli, IncrCmd incr) -> Response {
  return execute(db, cli, IncrByCmd{std::move(incr.key), 1});
}

static auto execute(DB &db, Client & /*cli*/, SetCmd set) -> Response {
  db.GetAndSet(std::move(set.key), std::move(set.val));
  return Token("OK");
}

static auto execute(DB &db, Client & /*cli*/, const StrLenCmd &strlen) -> Response {
  if (auto *val = db.Get(strlen.key)) {
    return Token(Integer(val->length()));
  }
  return Token(0);
}

static auto execute(DB &db, Client &cli, Command command) -> Response {
  return std::visit([&](auto command) { return execute(db, cli, std::move(command)); }, command);
}

auto Execute(DB &db, Client &client, Deserializer &query_reader) -> boost::asio::awaitable<Response> {
  auto executor = co_await boost::asio::this_coro::executor;
  Channel ch(executor);

  co_spawn(executor, query_reader.SendTokens(ch), boost::asio::detached);

  auto tok = co_await ch.async_receive(use_awaitable);
  auto it = ParseFuncs.find(get_str(tok));
  if (it == ParseFuncs.end()) {
    throw ExecutionException{"INVALID_COMMAND"};
  }

  const auto parse_func = it->second;
  auto command = co_await parse_func(ch);

  if (std::holds_alternative<EndOfCommand_t>(tok)) {
    throw ExecutionException("EXTRA_ARGUMENTS_TO_COMMAND");
  }

  co_return execute(db, client, std::move(command));
}
}  // namespace redispp