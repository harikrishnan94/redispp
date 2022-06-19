#include "resp_serde.h"

#include <fmt/format.h>

#include <boost/asio/use_awaitable.hpp>
#include <charconv>
#include <optional>

namespace redispp::resp {
auto Deserializer::SendTokens(Channel& ch) -> boost::asio::awaitable<void> {
  const auto msg_type = co_await dser_msg_type_marker();
  if (!msg_type) {
    co_await send_inline_tokens(ch);
  } else if (*msg_type != TokenTypeMarker::Array) {
    co_await send_token(co_await dser_single_token(*msg_type), ch);
  } else {
    const auto count = co_await dser_integer();
    if (count > 0) {
      for (Integer i = 0; i < count; i++) {
        const auto msg_type = co_await dser_msg_type_marker();
        if (!msg_type) {
          throw std::runtime_error("Encountered inline command inside array");
        }
        co_await send_token(co_await dser_single_token(*msg_type), ch);
      }
    } else {
      co_await send_token(NullArr, ch);
    }
  }
  co_return co_await send_token(EndOfCommand, ch);
}

auto Deserializer::send_token(Token tok, Channel& ch) -> boost::asio::awaitable<void> {
  co_await ch.async_send({}, std::move(tok), boost::asio::use_awaitable);
}

auto Deserializer::send_inline_tokens(Channel& ch) -> boost::asio::awaitable<void> {
  auto msg_str = co_await dser_any();
  std::string_view msg_ptr = msg_str;
  constexpr auto Space = ' ';

  while (true) {
    auto pos = msg_ptr.find(Space);
    if (pos == std::string_view::npos) {
      co_await send_token(String(msg_ptr), ch);
      break;
    }
    co_await send_token(String(msg_ptr.substr(0, pos)), ch);
    msg_ptr = msg_ptr.substr(pos + 1);
  }
}

auto Deserializer::dser_single_token(TokenTypeMarker msg_type) -> boost::asio::awaitable<Token> {
  switch (msg_type) {
    using enum TokenTypeMarker;

    case SimpleString:
      co_return co_await dser_simple_string();

    case Error: {
      auto str = co_await dser_simple_string();
      struct Error err = {std::move(str)};
      co_return err;
    }

    case Integer:
      co_return co_await dser_integer();

    case BulkString:
      co_return co_await dser_bulk_string(co_await dser_integer());

    case Array:
    default:
      throw std::runtime_error(fmt::format("Encountered wrong message type: {}", static_cast<char>(msg_type)));
  }
}

auto Deserializer::dser_msg_type_marker() -> boost::asio::awaitable<std::optional<TokenTypeMarker>> {
  co_await read_some();

  auto msg_type = static_cast<TokenTypeMarker>(m_mem[m_cursor]);
  if (msg_type != TokenTypeMarker::SimpleString && msg_type != TokenTypeMarker::Error &&
      msg_type != TokenTypeMarker::Integer && msg_type != TokenTypeMarker::BulkString &&
      msg_type != TokenTypeMarker::Array) {
    co_return std::nullopt;
  }

  m_cursor += 1;
  m_buflen -= 1;
  co_return msg_type;
}

auto Deserializer::dser_bulk_string(ptrdiff_t len) -> boost::asio::awaitable<Token> {
  if (len == -1) {
    co_return NullStr;
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

auto Deserializer::dser_any() -> boost::asio::awaitable<String> {
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

auto Deserializer::dser_integer() -> boost::asio::awaitable<Integer> {
  auto int_str = co_await dser_any();
  Integer i = 0;
  auto res = std::from_chars(int_str.begin().base(), int_str.end().base(), i);
  if (res.ec != std::errc{}) {
    throw std::system_error(make_error_code(res.ec));
  }

  co_return i;
}

auto Deserializer::read_some() -> boost::asio::awaitable<void> {
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
    auto n = co_await m_read_some(m_reader, m_mem.data() + readpos, m_mem.size() - readpos);
    m_buflen += n;
    readpos += n;
  }
}

auto Deserializer::copy_some(char* buf, size_t len) -> size_t {
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

// helper type for the visitor #4
template <class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};
// explicit deduction guide (not needed as of C++20)
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

auto Serializer::Serialize(const Token& tok) -> boost::asio::awaitable<void> {
  co_await std::visit([this](const auto& tok) { return this->serialize(tok); }, tok);
}

auto Serializer::SerializeArrayHeader(size_t elem_count) -> boost::asio::awaitable<void> {
  std::array<char, 30> count{};

  fmt::format_to(count.data(), "{}{}{}", static_cast<char>(TokenTypeMarker::Array), elem_count, MessagePartTerminator);
  co_await write(count.data(), count.size());
}

auto Serializer::SerializeNullArray(NullArr_t /*nullarr*/) -> boost::asio::awaitable<void> {
  return serialize(NullArr);
}

auto Serializer::serialize(NullArr_t /*nullarr*/) -> boost::asio::awaitable<void> {
  const std::string_view NullArray = "*-1";
  co_await write(NullArray.data(), NullArray.length());
  co_await write(MessagePartTerminator.data(), MessagePartTerminator.length());
}

auto Serializer::serialize(const String& s) -> boost::asio::awaitable<void> {
  std::array<char, 30> length{};

  fmt::format_to(
      length.data(), "{}{}{}", static_cast<char>(TokenTypeMarker::BulkString), s.length(), MessagePartTerminator);
  co_await write(length.data(), length.size());
  co_await write(s.data(), s.length());
  co_await write(MessagePartTerminator.data(), MessagePartTerminator.length());
}

auto Serializer::serialize_simple_string(const String& s) -> boost::asio::awaitable<void> {
  const auto msg_type = static_cast<char>(TokenTypeMarker::SimpleString);

  co_await write(&msg_type, sizeof(msg_type));
  co_await write(s.data(), s.length());
  co_await write(MessagePartTerminator.data(), MessagePartTerminator.length());
}

auto Serializer::serialize(const Error& err) -> boost::asio::awaitable<void> {
  const auto msg_type = static_cast<char>(TokenTypeMarker::Error);

  co_await write(&msg_type, sizeof(msg_type));
  co_await write(err.msg.data(), err.msg.length());
  co_await write(MessagePartTerminator.data(), MessagePartTerminator.length());
}

auto Serializer::serialize(Integer i) -> boost::asio::awaitable<void> {
  std::array<char, 30> int_str{};

  fmt::format_to(int_str.data(), "{}{}{}", static_cast<char>(TokenTypeMarker::Integer), i, MessagePartTerminator);
  co_await write(int_str.data(), int_str.size());
}

auto Serializer::serialize(NullStr_t /*nullstr*/) -> boost::asio::awaitable<void> {
  constexpr std::string_view NullString = "$-1";
  co_await write(NullString.data(), NullString.length());
  co_await write(MessagePartTerminator.data(), MessagePartTerminator.length());
}
}  // namespace redispp::resp