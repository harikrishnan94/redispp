#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/smart_ptr/local_shared_ptr.hpp>
#include <concepts>
#include <cstddef>
#include <memory_resource>
#include <optional>

namespace redispp::resp {
enum class TokenTypeMarker : char { SimpleString = '+', Error = '-', Integer = ':', BulkString = '$', Array = '*' };

using Integer = std::int64_t;
using String = std::pmr::string;

struct NullStr_t {
} constexpr NullStr;
struct NullArr_t {
} constexpr NullArr;
struct EndOfCommand_t {
} constexpr EndOfCommand;

struct Error {
  String msg;
};

using Token = std::variant<Integer, String, Error, NullStr_t, NullArr_t, EndOfCommand_t>;

static constexpr std::string_view MessagePartTerminator = "\r\n";

template <typename T>
concept Reader = requires(T a, char* buf, size_t len) {
  { ReadSome(a, buf, len) } -> std::same_as<boost::asio::awaitable<size_t>>;
};

using Channel = boost::asio::experimental::channel<void(boost::system::error_code, Token)>;

class Deserializer {
 public:
  explicit Deserializer(Reader auto& reader, std::pmr::memory_resource& alloc = *std::pmr::get_default_resource())
      : m_alloc(&alloc), m_reader(&reader), m_read_some(get_reader<std::decay_t<decltype(reader)>, false>()) {}

  explicit Deserializer(const Reader auto& reader, std::pmr::memory_resource& alloc = *std::pmr::get_default_resource())
      : m_alloc(&alloc),
        m_reader(
            const_cast<void*>(static_const<const void*>(&reader))),  // NOLINT(cppcoreguidelines-pro-type-const-cast)
        m_read_some(get_reader<std::decay_t<decltype(reader)>, true>()) {}

  auto SendTokens(boost::local_shared_ptr<Channel> ch) -> boost::asio::awaitable<void>;

 private:
  using read_some_t = boost::asio::awaitable<size_t> (*)(void* reader, char* buf, size_t len);

  template <Reader Reader, bool IsConst>
  static constexpr auto get_reader() noexcept -> read_some_t {
    return [](void* reader, char* buf, size_t len) {
      if constexpr (IsConst) {
        return ReadSome(*static_cast<const Reader*>(reader), buf, len);
      } else {
        return ReadSome(*static_cast<Reader*>(reader), buf, len);
      }
    };
  }

  auto send_token(Token tok, Channel& ch) -> boost::asio::awaitable<void>;
  auto send_inline_tokens(Channel& ch) -> boost::asio::awaitable<void>;

  auto dser_single_token(TokenTypeMarker msg_type) -> boost::asio::awaitable<Token>;
  auto dser_msg_type_marker() -> boost::asio::awaitable<std::optional<TokenTypeMarker>>;
  auto dser_bulk_string(ptrdiff_t len) -> boost::asio::awaitable<Token>;
  auto dser_simple_string() -> boost::asio::awaitable<String> { co_return co_await dser_any(); }
  auto dser_any() -> boost::asio::awaitable<String>;
  auto dser_integer() -> boost::asio::awaitable<Integer>;

  auto read_some() -> boost::asio::awaitable<void>;
  auto copy_some(char* buf, size_t len) -> size_t;

  static constexpr auto BufferSize = 1024;
  static_assert(BufferSize >= MessagePartTerminator.size());

  std::pmr::memory_resource* m_alloc;
  std::array<char, BufferSize> m_mem{};
  size_t m_cursor = 0;
  size_t m_buflen = 0;
  void* m_reader = nullptr;
  read_some_t m_read_some = nullptr;
};

template <typename T>
concept Writer = requires(T a, const char* buf, size_t len) {
  { Write(a, buf, len) } -> std::same_as<boost::asio::awaitable<void>>;
};

class Serializer {
 public:
  explicit Serializer(Writer auto& writer)
      : m_writer(&writer), m_write(get_writer<std::decay_t<decltype(writer)>, false>()) {}

  explicit Serializer(const Writer auto& writer)
      : m_writer(
            const_cast<void*>(static_cast<const void*>(&writer))),  // NOLINT(cppcoreguidelines-pro-type-const-cast)
        m_write(get_writer<std::decay_t<decltype(writer)>, true>()) {}

  auto SerializeArrayHeader(size_t elem_count) -> boost::asio::awaitable<void>;
  auto SerializeNullArray(NullArr_t) -> boost::asio::awaitable<void>;

  auto Serialize(const Token& tok) -> boost::asio::awaitable<void>;

 private:
  using write_t = boost::asio::awaitable<void> (*)(void* writer, const char* buf, size_t len);

  auto serialize(Integer i) -> boost::asio::awaitable<void>;
  auto serialize_simple_string(const String& s) -> boost::asio::awaitable<void>;
  auto serialize(const Error& err) -> boost::asio::awaitable<void>;

  auto serialize(const String& s) -> boost::asio::awaitable<void>;
  auto serialize(NullStr_t) -> boost::asio::awaitable<void>;
  auto serialize(NullArr_t) -> boost::asio::awaitable<void>;
  static auto serialize(EndOfCommand_t /*eoc*/) noexcept -> boost::asio::awaitable<void> { co_return; }

  auto write(const char* buf, size_t len) -> boost::asio::awaitable<void> { return m_write(m_writer, buf, len); }

  template <Writer Writer, bool IsConst>
  static constexpr auto get_writer() noexcept -> write_t {
    return [](void* writer, const char* buf, size_t len) {
      if constexpr (IsConst) {
        return Write(*static_cast<const Writer*>(writer), buf, len);
      } else {
        return Write(*static_cast<Writer*>(writer), buf, len);
      }
    };
  }

  void* m_writer = nullptr;
  write_t m_write = nullptr;
};

}  // namespace redispp::resp