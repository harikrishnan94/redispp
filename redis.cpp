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
#include <charconv>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <variant>
#include <vector>

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
  explicit MessageReader(std::istream& in) : m_in(&in) {}

  auto ReadMessage() -> Message {
    const auto msg_type = read_msg_type_marker();
    if (msg_type != MessageTypeMarker::Array) {
      return read_single_message(msg_type);
    }

    const auto count = read_integer();
    MessageArray msgs(count);

    for (auto& msg : msgs) {
      const auto msg_type = read_msg_type_marker();
      msg = read_single_message(msg_type);
    }

    return msgs;
  }

 private:
  auto read_msg_type_marker() -> MessageTypeMarker {
    read_more();

    auto msg_type = static_cast<MessageTypeMarker>(m_mem[m_cursor]);
    m_cursor += 1;
    m_buflen -= 1;

    return msg_type;
  }

  auto read_bulk_string(size_t len) -> String {
    String str(len + MessagePartTerminator.length(), '\0');
    size_t copied = 0;

    while (copied < str.length()) {
      read_more();
      copied += copy_some(str.data() + copied, str.length() - copied);
    }

    if (str.rbegin()[0] != MessagePartTerminator[1] && str.rbegin()[1] != MessagePartTerminator[0]) {
      throw std::runtime_error("Invalid Bulk string: " + str);
    }

    // Remove trailing 'MessagePartTerminator'
    str.pop_back();
    str.pop_back();

    return str;
  }

  auto read_simple_string() -> String { return read_one_part(); }

  auto read_integer() -> Integer {
    auto int_str = read_one_part();
    Integer i = 0;
    auto res = std::from_chars(int_str.begin().base(), int_str.end().base(), i);
    if (res.ec != std::errc{}) {
      throw std::system_error(make_error_code(res.ec));
    }

    return i;
  }

  auto read_single_message(MessageTypeMarker msg_type) -> SingularMessage {
    switch (msg_type) {
      using enum MessageTypeMarker;

      case SimpleString:
        return read_simple_string();

      case Error:
        return ErrorMessage{read_one_part()};

      case Integer:
        return read_integer();

      case BulkString:
        return read_bulk_string(read_integer());

      case Array:
      default:
        throw std::runtime_error(fmt::format("Encountered wrong message type: {}", static_cast<char>(msg_type)));
    }
  }

  auto read_one_part() -> String {
    String msg_part;

    while (true) {
      read_more();

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

    return msg_part;
  }

  auto read_more() -> void {
    if (m_buflen + m_cursor == m_mem.size()) {
      return;
    }
    if (m_buflen == 1) {
      m_mem[0] = m_mem[m_cursor];
      m_cursor = 1;
      m_buflen = 1;
    }
    auto readpos = m_cursor;
    while (m_buflen < MessagePartTerminator.length()) {
      auto n = m_in->readsome(m_mem.data() + readpos, m_mem.size() - readpos);
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
  std::array<char, BufferSize> m_mem{};
  size_t m_cursor = 0;
  size_t m_buflen = 0;
  std::istream* m_in;
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

auto run_session(std::istream& in) -> void {
  try {
    MessageReader reader(in);
    while (in) {
      auto msg = reader.ReadMessage();
      fmt::print("{}", MessageToString(msg));
    }
  } catch (std::exception& e) {
    std::printf("echo Exception: %s\n", e.what());
  }
}

// awaitable<void> listener() {
//   auto executor = co_await this_coro::executor;
//   tcp::acceptor acceptor(executor, {tcp::v4(), 55555});
//   for (;;) {
//     tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
//     co_spawn(executor, run_session(std::move(socket)), detached);
//   }
// }

auto main() -> int {
  try {
    std::istringstream input(
        ":1000\r\n$306\r\n// awaitable<void> listener() {"
        "//   auto executor = co_await this_coro::executor;"
        "//   tcp::acceptor acceptor(executor, {tcp::v4(), 55555});"
        "//   for (;;) {"
        "//     tcp::socket socket = co_await acceptor.async_accept(use_awaitable);"
        "//     co_spawn(executor, run_session(std::move(socket)), detached);"
        "//   }"
        "// }\r\n+OK\r\n-Error message\r\n");
    run_session(input);
  } catch (std::exception& e) {
    std::printf("Exception: %s\n", e.what());
  }
}
