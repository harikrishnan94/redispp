#include <fmt/core.h>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>

#include "db.h"
#include "exec.h"
#include "resp_serde.h"

using boost::asio::awaitable;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::use_awaitable;
using boost::asio::ip::tcp;
namespace this_coro = boost::asio::this_coro;

#if defined(BOOST_ASIO_ENABLE_HANDLER_TRACKING)
#define use_awaitable boost::asio::use_awaitable_t(__FILE__, __LINE__, __PRETTY_FUNCTION__)
#endif

namespace boost::asio {
auto ReadSome(tcp::socket& socket, char* buf, size_t bufsize) -> awaitable<size_t> {
  return socket.async_read_some(boost::asio::buffer(buf, bufsize), use_awaitable);
}

auto Write(tcp::socket& socket, const char* buf, size_t bufsize) -> awaitable<void> {
  co_await boost::asio::async_write(socket, boost::asio::buffer(buf, bufsize), use_awaitable);
}
}  // namespace boost::asio

auto send_result(redispp::Response response, tcp::socket& socket) -> boost::asio::awaitable<void> {
  redispp::resp::Serializer serializer(socket);
  co_await response.Serialize(serializer);
}

auto run_session(redispp::DB& db, tcp::socket socket) -> awaitable<void> {
  try {
    auto executor = co_await this_coro::executor;
    redispp::Client client;
    redispp::resp::Deserializer deserializer(socket);

    for (;;) {
      auto response = co_await redispp::Execute(db, client, deserializer);
      co_spawn(executor, send_result(std::move(response), socket), detached);
    }
  } catch (std::exception& e) {
    fmt::print("echo Exception: {}\n", e.what());
  }
}

auto listener(redispp::DB& db) -> awaitable<void> {
  auto executor = co_await this_coro::executor;
  tcp::acceptor acceptor(executor, {tcp::v4(), 55555});
  for (;;) {
    tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
    co_spawn(executor, run_session(db, std::move(socket)), detached);
  }
}

auto main() -> int {
  try {
    boost::asio::io_context io_context(1);

    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) { io_context.stop(); });

    redispp::DB db;
    co_spawn(io_context, listener(db), detached);
    io_context.run();

  } catch (std::exception& e) {
    fmt::print("Exception: {}\n", e.what());
  }
}
