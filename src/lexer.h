#pragma once

#include <fmt/format.h>

#include <array>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/detail/error_code.hpp>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <memory_resource>
#include <optional>
#include <string_view>
#include <variant>

namespace redispp {
enum class TokenTypeMarker : char { SimpleString = '+', Error = '-', Integer = ':', BulkString = '$', Array = '*' };
static constexpr std::string_view MessagePartTerminator = "\r\n";

using Integer = std::int64_t;
using String = std::pmr::string;

struct null {};
struct end_of_command {};
struct ErrorMessage {
  String msg;
};

using Token = std::variant<Integer, String, ErrorMessage, null, end_of_command>;

template <typename T>
concept IReader = requires(T a, char* buf, size_t len) {
  { ReadSome(a, buf, len) } -> std::same_as<boost::asio::awaitable<size_t>>;
};

using Channel = boost::asio::experimental::channel<void(boost::system::error_code, Token)>;

template <IReader Reader>
class Parser {
 public:
  // NOLINTNEXTLINE(*-member-init)
  explicit Parser(Reader& reader, std::pmr::memory_resource& alloc = *std::pmr::get_default_resource())
      : m_alloc(&alloc), m_reader(&reader) {}

  auto ParseMessage(Channel& ch) -> boost::asio::awaitable<void> {
    const auto msg_type = co_await read_msg_type_marker();
    if (!msg_type) {
      co_await send_inline_command(ch);
    } else if (*msg_type != TokenTypeMarker::Array) {
      co_await send_token(co_await read_single_token(*msg_type), ch);
    } else {
      const auto count = co_await read_integer();
      if (count > 0) {
        for (Integer i = 0; i < count; i++) {
          const auto msg_type = co_await read_msg_type_marker();
          if (!msg_type) {
            throw std::runtime_error("Encountered inline command inside array");
          }
          co_await send_token(co_await read_single_token(*msg_type), ch);
        }
      }
    }
    co_return co_await send_token(end_of_command{}, ch);
  }

 private:
  auto send_token(Token tok, Channel& ch) -> boost::asio::awaitable<void> {
    co_await ch.async_send({}, std::move(tok), boost::asio::use_awaitable);
  }

  auto read_single_token(TokenTypeMarker msg_type) -> boost::asio::awaitable<Token> {
    switch (msg_type) {
      using enum TokenTypeMarker;

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

  auto read_msg_type_marker() -> boost::asio::awaitable<std::optional<TokenTypeMarker>> {
    co_await read_some();

    auto msg_type = static_cast<TokenTypeMarker>(m_mem[m_cursor]);
    switch (msg_type) {
      default:
        co_return std::nullopt;

      case TokenTypeMarker::SimpleString:
      case TokenTypeMarker::Error:
      case TokenTypeMarker::Integer:
      case TokenTypeMarker::BulkString:
      case TokenTypeMarker::Array:
        break;
    }
    m_cursor += 1;
    m_buflen -= 1;

    co_return msg_type;
  }

  auto read_bulk_string(ptrdiff_t len) -> boost::asio::awaitable<Token> {
    if (len == -1) {
      co_return null{};
    }
    String str(len + MessagePartTerminator.length(), '\0', m_alloc);
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

  auto read_simple_string() -> boost::asio::awaitable<String> { co_return co_await read_one_part(); }

  auto read_integer() -> boost::asio::awaitable<Integer> {
    auto int_str = co_await read_one_part();
    Integer i = 0;
    auto res = std::from_chars(int_str.begin().base(), int_str.end().base(), i);
    if (res.ec != std::errc{}) {
      throw std::system_error(make_error_code(res.ec));
    }

    co_return i;
  }

  auto send_inline_command(Channel& ch) -> boost::asio::awaitable<void> {
    auto msg_str = co_await read_one_part();
    std::string_view msg_ptr = msg_str;
    constexpr auto Space = ' ';

    while (true) {
      auto pos = msg_ptr.find(Space);
      if (pos == std::string_view::npos) {
        co_await ch.async_send({}, String(msg_ptr), boost::asio::use_awaitable);
        break;
      }
      co_await ch.async_send({}, String(msg_ptr.substr(0, pos)), boost::asio::use_awaitable);
      msg_ptr = msg_ptr.substr(pos + 1);
    }

    co_await ch.async_send({}, end_of_command{}, boost::asio::use_awaitable);
  }

  auto read_one_part() -> boost::asio::awaitable<String> {
    String msg_part(m_alloc);

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
}  // namespace redispp