#include <fmt/core.h>

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/detail/chrono.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/error_code.hpp>
#include <cstddef>
#include <string_view>
#include <variant>

#include "resp_serde.h"

using namespace boost::system;
using namespace boost::asio;
using namespace boost::asio::experimental;
using namespace boost::asio::experimental::awaitable_operators;

struct StringReadState {
  std::string_view str;
  size_t cursor = 0;
};

auto ReadSome(StringReadState &rs, char *buf, size_t len) -> awaitable<size_t> {
  const auto readlen = std::min(len, rs.str.length() - rs.cursor);
  std::copy_n(rs.str.data() + rs.cursor, readlen, buf);
  rs.cursor += readlen;
  co_return readlen;
}

struct StdOutWriter_t {
} constexpr StdOutWriter;

auto Write(StdOutWriter_t /*StdOutWriter*/, const char *buf, size_t len) -> awaitable<void> {
  fmt::print("{}", std::string_view(buf, len));
  co_return;
}

auto print(redispp::resp::Channel &ch) -> awaitable<void> {
  redispp::resp::Serializer serializer(StdOutWriter);
  while (true) {
    auto tok = co_await ch.async_receive(use_awaitable);
    co_await serializer.Serialize(tok);
    if (std::holds_alternative<redispp::resp::EndOfCommand_t>(tok)) {
      break;
    }
  }
}

auto run(io_context &ioc, std::string_view commands) -> awaitable<void> {
  StringReadState rs{commands};
  redispp::resp::Deserializer deserializer(rs);
  redispp::resp::Channel ch(ioc);

  co_await (deserializer.SendTokens(ch) || print(ch));
}

auto main() -> int {
  try {
    io_context io_context(1);

    const std::string_view commands =
        "*5\r\n:1000\r\n$306\r\n// awaitable<void> listener() {"
        "//   auto executor = co_await this_coro::executor;"
        "//   tcp::acceptor acceptor(executor, {tcp::v4(), 55555});"
        "//   for (;;) {"
        "//     tcp::socket socket = co_await acceptor.async_accept(use_awaitable);"
        "//     co_spawn(executor, run_session(std::move(socket)), detached);"
        "//   }"
        "// }\r\n+OK\r\n$-1\r\n-Error message\r\n";
    co_spawn(io_context, run(io_context, commands), detached);
    io_context.run();

  } catch (std::exception &e) {
    fmt::print("Exception: {}\n", e.what());
  }
}