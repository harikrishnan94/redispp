//
// echo_server.cpp
// ~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2022 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <boost/asio/buffer.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>
#include <charconv>
#include <cstdio>
#include <string>
#include <system_error>
#include <variant>
#include <vector>

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
using String = std::string;
struct ErrorMessage {
  String msg;
};
using SingularMessage = std::variant<Integer, String, ErrorMessage>;
using MessageArray = std::vector<SingularMessage>;
using Message = std::variant<std::monostate, SingularMessage, MessageArray>;

static constexpr std::string_view MessagePartTerminator = "\r\n";

class MessageReader {
 public:
  explicit MessageReader(tcp::socket& socket) : m_socket(&socket) {}

  auto ReadMessage() -> awaitable<Message> {
    const auto msg_type = co_await read_msg_type_marker();
    if (msg_type != MessageTypeMarker::Array) {
      co_return co_await read_single_message(msg_type);
    }

    const auto count = co_await read_integer();
    MessageArray msgs(count);

    for (auto& msg : msgs) {
      const auto msg_type = co_await read_msg_type_marker();
      msg = co_await read_single_message(msg_type);
    }

    co_return msgs;
  }

 private:
  auto read_msg_type_marker() -> awaitable<MessageTypeMarker> {
    co_await read_some();

    auto msg_type = static_cast<MessageTypeMarker>(m_mem[m_cursor]);
    m_cursor += 1;
    m_buflen -= 1;

    co_return msg_type;
  }

  auto read_bulk_string(size_t len) -> awaitable<String> {
    String str(len + MessagePartTerminator.length(), '\0');
    size_t copied = 0;

    while (copied < str.length()) {
      co_await read_some();
      copied += copy_some(str.data() + copied, str.length() - copied);
    }

    if (str.rbegin()[0] != MessagePartTerminator[1] && str.rbegin()[1] != MessagePartTerminator[0]) {
      throw std::runtime_error("Invalid Bulk string: " + str);
    }

    // Remove trailing 'MessagePartTerminator'
    str.pop_back();
    str.pop_back();

    co_return str;
  }

  auto read_simple_string() -> awaitable<String> { co_return co_await read_one_part(); }

  auto read_integer() -> awaitable<Integer> {
    auto int_str = co_await read_one_part();
    Integer i = 0;
    auto res = std::from_chars(int_str.begin().base(), int_str.end().base(), i);
    if (res.ec != std::errc{}) {
      throw std::system_error(make_error_code(res.ec));
    }

    co_return i;
  }

  auto read_single_message(MessageTypeMarker msg_type) -> awaitable<SingularMessage> {
    switch (msg_type) {
      using enum MessageTypeMarker;

      case SimpleString:
        co_return co_await read_simple_string();

      case Error: {
        auto str = co_await read_simple_string();
        co_return ErrorMessage{std::move(str)};
      }

      case Integer:
        co_return co_await read_integer();

      case BulkString:
        co_return co_await read_bulk_string(co_await read_integer());

      case Array:
      default:
        throw std::runtime_error(fmt::format("Encountered wrong message type: {}", static_cast<char>(msg_type)));
    }
  }

  auto read_one_part() -> awaitable<String> {
    String msg_part;

    while (true) {
      co_await read_some();

      std::string_view buf = {static_cast<const char*>(m_mem.data() + m_cursor), m_buflen};
      const auto dl_pos = buf.find(MessagePartTerminator);

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

  auto read_some() -> awaitable<void> {
    if (m_buflen == 0) {
      m_cursor = 0;
    }
    if (m_buflen == 1) {
      m_mem[0] = m_mem[m_cursor];
      m_cursor = 0;
      m_buflen = 1;
    }
    auto readpos = m_cursor + m_buflen;
    while (m_buflen < MessagePartTerminator.length()) {
      boost::asio::mutable_buffer buf(m_mem.data() + readpos, m_mem.size() - readpos);
      auto n = co_await m_socket->async_read_some(buf, use_awaitable);
      m_buflen += n;
      readpos += n;
    }
  }

  auto copy_some(char* buf, size_t len) -> size_t {
    auto readlen = std::min(len, m_buflen);
    std::copy_n(&m_mem[m_cursor], readlen, buf);
    m_buflen -= readlen;
    if (m_buflen == 0) {
      m_cursor = 0;
    } else {
      m_cursor += readlen;
    }
    return readlen;
  }

  static constexpr auto BufferSize = 1024;
  static_assert(BufferSize >= MessagePartTerminator.size());

  std::array<char, BufferSize> m_mem{};
  size_t m_cursor = 0;
  size_t m_buflen = 0;
  tcp::socket* m_socket;
};

auto MessageToString(const String& s) -> std::string {
  return fmt::format("{}{}{}{}{}",
                     static_cast<char>(MessageTypeMarker::BulkString),
                     s.length(),
                     MessagePartTerminator,
                     s,
                     MessagePartTerminator);
}

auto MessageToString(const Integer& i) -> std::string {
  return fmt::format("{}{}{}", static_cast<char>(MessageTypeMarker::Integer), i, MessagePartTerminator);
}

auto MessageToString(const SingularMessage& msg) -> std::string {
  return std::visit([](const auto& msg) { return MessageToString(msg); }, msg);
}

auto MessageToString(const ErrorMessage& msg) -> std::string {
  return fmt::format("{}{}{}", static_cast<char>(MessageTypeMarker::Error), msg.msg, MessagePartTerminator);
}

auto MessageToString(const MessageArray& msgs) -> std::string {
  std::string str(1, static_cast<char>(MessageTypeMarker::Array));

  str += MessageToString(msgs.size());

  for (const auto& msg : msgs) {
    str += MessageToString(msg);
  }

  return str;
}

auto MessageToString(const Message& msg) -> std::string {
  return std::visit([](const auto& msg) { return MessageToString(msg); }, msg);
}

auto run_session(tcp::socket socket) -> awaitable<void> {
  try {
    MessageReader reader(socket);
    for (;;) {
      auto msg = co_await reader.ReadMessage();
      auto str = MessageToString(msg);
      co_await boost::asio::async_write(socket, boost::asio::buffer(str.data(), str.size()), use_awaitable);
    }
  } catch (std::exception& e) {
    fmt::print("echo Exception: {}\n", e.what());
  }
}

auto listener() -> awaitable<void> {
  auto executor = co_await this_coro::executor;
  tcp::acceptor acceptor(executor, {tcp::v4(), 55555});
  for (;;) {
    tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
    co_spawn(executor, run_session(std::move(socket)), detached);
  }
}

auto main() -> int {
  try {
    boost::asio::io_context io_context(1);

    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) { io_context.stop(); });

    co_spawn(io_context, listener(), detached);
    io_context.run();

  } catch (std::exception& e) {
    fmt::print("Exception: {}\n", e.what());
  }
}
