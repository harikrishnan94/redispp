#pragma once

#include <fmt/format.h>

#include <array>
#include <boost/asio/awaitable.hpp>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <memory_resource>
#include <optional>
#include <string_view>
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
using Message = std::variant<std::monostate, SingularMessage, MessageArray>;

static constexpr std::string_view MessagePartTerminator = "\r\n";

template <typename T>
concept IReader = requires(T a, char* buf, size_t len) {
  { ReadSome(a, buf, len) } -> std::same_as<boost::asio::awaitable<size_t>>;
};

template <IReader Reader>
class Parser {
 public:
  // NOLINTNEXTLINE(*-member-init)
  explicit Parser(Reader& reader, std::pmr::memory_resource& alloc = *std::pmr::get_default_resource())
      : m_alloc(&alloc), m_reader(&reader) {}

  auto ParseMessage() -> boost::asio::awaitable<Message> {
    const auto msg_type = co_await read_msg_type_marker();
    if (msg_type != MessageTypeMarker::Array) {
      co_return co_await read_single_message(msg_type);
    }

    const auto count = co_await read_integer();
    if (count < 0) {
      co_return MessageArray{};
    }
    std::pmr::vector<SingularMessage> msgs(count, m_alloc);

    for (auto& msg : msgs) {
      const auto msg_type = co_await read_msg_type_marker();
      msg = co_await read_single_message(msg_type);
    }

    co_return std::move(msgs);
  }

 private:
  auto read_msg_type_marker() -> boost::asio::awaitable<MessageTypeMarker> {
    co_await read_some();

    auto msg_type = static_cast<MessageTypeMarker>(m_mem[m_cursor]);
    m_cursor += 1;
    m_buflen -= 1;

    co_return msg_type;
  }

  auto read_bulk_string(ptrdiff_t len) -> boost::asio::awaitable<String> {
    if (len == -1) {
      co_return std::nullopt;
    }
    Str str(len + MessagePartTerminator.length(), '\0', m_alloc);
    size_t copied = 0;

    while (copied < str.length()) {
      co_await read_some();
      copied += copy_some(str.data() + copied, str.length() - copied);
    }

    if (str.rbegin()[0] != MessagePartTerminator[1] && str.rbegin()[1] != MessagePartTerminator[0]) {
      throw std::runtime_error(fmt::format("Invalid Bulk string: {}", str));
    }

    // Remove trailing 'MessagePartTerminator'
    str.pop_back();
    str.pop_back();

    co_return std::move(str);
  }

  auto read_simple_string() -> boost::asio::awaitable<Str> { co_return co_await read_one_part(); }

  auto read_integer() -> boost::asio::awaitable<Integer> {
    auto int_str = co_await read_one_part();
    Integer i = 0;
    auto res = std::from_chars(int_str.begin().base(), int_str.end().base(), i);
    if (res.ec != std::errc{}) {
      throw std::system_error(make_error_code(res.ec));
    }

    co_return i;
  }

  auto read_single_message(MessageTypeMarker msg_type) -> boost::asio::awaitable<SingularMessage> {
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

  auto read_one_part() -> boost::asio::awaitable<Str> {
    Str msg_part(m_alloc);

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

  auto read_some() -> boost::asio::awaitable<void> {
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
      auto n = co_await ReadSome(*m_reader, m_mem.data() + readpos, m_mem.size() - readpos);
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

  std::pmr::memory_resource* m_alloc;
  std::array<char, BufferSize> m_mem;
  size_t m_cursor = 0;
  size_t m_buflen = 0;
  Reader* m_reader;
};

template <typename T>
concept IWriter = requires(T a, const char* buf, size_t len) {
  { Write(a, buf, len) } -> std::same_as<boost::asio::awaitable<void>>;
};

auto Write(IWriter auto& writer, const Str& s) -> boost::asio::awaitable<void> {
  const auto msg_type = static_cast<char>(MessageTypeMarker::SimpleString);

  co_await Write(writer, &msg_type, sizeof(msg_type));
  co_await Write(writer, s.data(), s.length());
  co_await Write(writer, MessagePartTerminator.data(), MessagePartTerminator.length());
}

auto Write(IWriter auto& writer, const String& s) -> boost::asio::awaitable<void> {
  if (!s) {
    const std::string_view NullString = "$-1";
    co_await Write(writer, NullString.data(), NullString.length());
    co_await Write(writer, MessagePartTerminator.data(), MessagePartTerminator.length());
    co_return;
  }
  std::array<char, 30> length{};

  fmt::format_to(
      length.data(), "{}{}{}", static_cast<char>(MessageTypeMarker::BulkString), s->length(), MessagePartTerminator);
  co_await Write(writer, length.data(), length.size());
  co_await Write(writer, s->data(), s->length());
  co_await Write(writer, MessagePartTerminator.data(), MessagePartTerminator.length());
}

auto Write(IWriter auto& writer, const Integer& i) -> boost::asio::awaitable<void> {
  std::array<char, 30> int_str{};

  fmt::format_to(int_str.data(), "{}{}{}", static_cast<char>(MessageTypeMarker::Integer), i, MessagePartTerminator);
  co_await Write(writer, int_str.data(), int_str.size());
}

auto Write(IWriter auto& writer, const ErrorMessage& msg) -> boost::asio::awaitable<void> {
  const auto msg_type = static_cast<char>(MessageTypeMarker::Error);

  co_await Write(writer, &msg_type, sizeof(msg_type));
  co_await Write(writer, msg.msg.data(), msg.msg.length());
  co_await Write(writer, MessagePartTerminator.data(), MessagePartTerminator.length());
}

auto Write(IWriter auto& writer, const SingularMessage& msg) -> boost::asio::awaitable<void> {
  co_await std::visit([&](const auto& msg) { return Write(writer, msg); }, msg);
}

auto Write(IWriter auto& writer, const MessageArray& msgs) -> boost::asio::awaitable<void> {
  if (!msgs) {
    const std::string_view NullArray = "*-1";
    co_await Write(writer, NullArray.data(), NullArray.length());
    co_await Write(writer, MessagePartTerminator.data(), MessagePartTerminator.length());
    co_return;
  }

  std::array<char, 30> count{};

  fmt::format_to(
      count.data(), "{}{}{}", static_cast<char>(MessageTypeMarker::Array), msgs->size(), MessagePartTerminator);
  co_await Write(writer, count.data(), count.size());

  for (const auto& msg : *msgs) {
    co_await Write(writer, msg);
  }
}

auto Write(IWriter auto& writer, const Message& msg) -> boost::asio::awaitable<void> {
  co_await std::visit([&](const auto& msg) { return Write(writer, msg); }, msg);
}
}  // namespace redispp