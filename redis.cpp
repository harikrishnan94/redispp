//
// echo_server.cpp
// ~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2022 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/write.hpp>
#include <charconv>
#include <cstdio>
#include <system_error>
#include <variant>
#include <fmt/format.h>

using boost::asio::awaitable;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::use_awaitable;
using boost::asio::ip::tcp;
namespace this_coro = boost::asio::this_coro;

#if defined(BOOST_ASIO_ENABLE_HANDLER_TRACKING)
#define use_awaitable boost::asio::use_awaitable_t(__FILE__, __LINE__, __PRETTY_FUNCTION__)
#endif

enum class MessageTypeMarker : char { SimpleString = '+', Error = '-', Integer = ':', BulkString = '$', Array = '*' };

using Integer = std::int64_t;
using SingularMessage = std::variant<Integer, std::string>;
using MessageArray = std::vector<SingularMessage>;
struct ErrorMessage {
  std::string msg;
};
using Message = std::variant<std::monostate, SingularMessage, ErrorMessage, MessageArray>;

static constexpr std::string_view MessagePartTerminator = "\r\n";

class MessageReader {
 public:
  explicit MessageReader(tcp::socket& socket) : m_socket(&socket) {}

  auto ReadMessage() -> awaitable<Message> {
    const auto msg_type = co_await read_msg_type_marker();
    if (msg_type != MessageTypeMarker::Array) {
      co_return read_single_message();
    }

    const auto count = co_await read_integer();
    MessageArray msgs(count);

    for (auto& msg : msgs) {
      msg = co_await read_single_message();
    }

    co_return msgs;
  }

 private:
  auto read_msg_type_marker() -> awaitable<MessageTypeMarker> {
    co_await read_more();

    auto msg_type = static_cast<MessageTypeMarker>(m_mem[m_cursor]);
    m_cursor += 1;
    m_buflen -= 1;

    co_return msg_type;
  }

  auto read_bulk_string(size_t len) -> awaitable<std::string> {
    std::string str(len + MessagePartTerminator.length(), '\0');

    co_await boost::asio::async_read(*m_socket, boost::asio::buffer(str.data(), str.length()), use_awaitable);
    m_buflen = 0;
    m_cursor = 0;

    if (str.rbegin()[0] != MessagePartTerminator[1] && str.rbegin()[1] != MessagePartTerminator[0]) {
      throw std::runtime_error("Invalid Bulk string: " + str);
    }

    co_return str;
  }

  auto read_simple_string() -> awaitable<std::string> { co_return co_await read_one_part(); }

  auto read_integer() -> awaitable<Integer> {
    auto int_str = co_await read_one_part();
    Integer i = 0;
    auto res = std::from_chars(int_str.begin().base(), int_str.end().base(), i);
    if (res.ec != std::errc{}) {
      throw std::system_error(make_error_code(res.ec));
    }

    co_return i;
  }

  auto read_single_message() -> awaitable<SingularMessage> {
    const auto msg_type = co_await read_msg_type_marker();
    switch (msg_type) {
      using enum MessageTypeMarker;

      case SimpleString:
        co_return read_simple_string();

      case Error:
        co_return ErrorMessage{co_await read_one_part()};

      case Integer:
        co_return read_integer();

      case BulkString:
        co_return read_bulk_string(co_await read_integer());

      case Array:
        throw std::runtime_error("Encountered wrong message type: " + std::to_string(static_cast<char>(msg_type)));
    }
  }

  auto read_one_part() -> awaitable<std::string> {
    std::string msg_part;

    while (true) {
      co_await read_more();

      std::string_view buf = {static_cast<const char*>(m_mem.data() + m_cursor), m_buflen};
      const auto dl_pos = buf.find_first_of(MessagePartTerminator);

      if (dl_pos != std::string_view::npos) {
        msg_part += buf.substr(0, dl_pos);
        m_cursor += dl_pos + MessagePartTerminator.length();
        m_buflen -= dl_pos + MessagePartTerminator.length();
        break;
      }

      if (buf.back() == MessagePartTerminator[0]) {
        buf = buf.substr(0, buf.size() - 1);
      }
      msg_part += buf;
      m_cursor += buf.size();
      m_buflen -= buf.size();
    }

    co_return msg_part;
  }

  auto read_more() -> awaitable<void> {
    if (m_buflen == 1) {
      m_mem[0] = m_mem[m_cursor];
      m_cursor = 1;
      m_buflen = 1;
    }
    while (m_buflen < MessagePartTerminator.length()) {
      boost::asio::mutable_buffer buf(m_mem.data() + m_cursor, m_mem.size() - m_cursor - m_buflen);
      co_await m_socket->async_read_some(buf, use_awaitable);
    }
  }

  static constexpr auto BufferSize = 1024;
  std::array<char, BufferSize> m_mem{};
  size_t m_cursor = 0;
  size_t m_buflen = 0;
  tcp::socket* m_socket;
};

auto WriteMessage(const std::string& s) -> std::string {
  return 
}

auto to_string(const SingularMessage& msg) -> std::string {
  return std::visit([](const auto& msg) { return to_string(msg); }, msg);
}

auto to_string(const ErrorMessage& msg) -> std::string {
  return msg.msg;
}

auto to_string(const MessageArray& msgs) -> std::string {

}

auto to_string(const Message& msg) -> std::string {}

auto run_session(tcp::socket socket) -> awaitable<void> {
  try {
    MessageReader reader(socket);
    for (;;) {
      auto msg = co_await reader.ReadMessage();
    }
  } catch (std::exception& e) {
    std::printf("echo Exception: %s\n", e.what());
  }
}

awaitable<void> listener() {
  auto executor = co_await this_coro::executor;
  tcp::acceptor acceptor(executor, {tcp::v4(), 55555});
  for (;;) {
    tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
    co_spawn(executor, run_session(std::move(socket)), detached);
  }
}

int main() {
  try {
    boost::asio::io_context io_context(1);

    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) { io_context.stop(); });

    co_spawn(io_context, listener(), detached);

    io_context.run();
  } catch (std::exception& e) {
    std::printf("Exception: %s\n", e.what());
  }
}
