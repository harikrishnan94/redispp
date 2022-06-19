#include "exec.h"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <variant>

#include "resp_serde.h"

using namespace boost::asio::experimental::awaitable_operators;

namespace redispp {
// class ExecutionException : std::exception {
//  public:
//   explicit ExecutionException(Str error) : m_error(std::move(error)) {}

//   [[nodiscard]] auto what() const noexcept -> const char * override { return m_error.c_str(); }

//  private:
//   Str m_error;
// };

// static auto to_int(std::string_view str) -> std::optional<Integer> {
//   Integer i;
//   auto res = std::from_chars(str.begin(), str.end(), i);

//   if (res.ec == std::errc{}) {
//     throw ExecutionException{"CONVERSION_ERROR Invalid Integer"};
//   }
//   return i;
// }

// static void to_str(std::pmr::string &str, Integer i) {
//   str.clear();
//   fmt::format_to(std::back_inserter(str), "{}", i);
// }

// static auto get_int(SingularMessage msg) -> Integer {
//   if (auto *i = std::get_if<Integer>(&msg)) {
//     return *i;
//   }
//   throw ExecutionException{"WRONG_INPUT_TYPE Expected Integer"};
// }

// static auto get_str(SingularMessage msg) -> Str {
//   if (auto *s = std::get_if<Str>(&msg)) {
//     return std::move(*s);
//   }
//   if (auto *s = std::get_if<String>(&msg)) {
//     if (!s->has_value()) {
//       throw ExecutionException{"EMPTY_INPUT Expected NonNull String"};
//     }
//     return std::move(**s);
//   }
//   throw ExecutionException{"WRONG_INPUT_TYPE Expected String"};
// }

// template <typename RealCommand>
// static auto parse(MessageArray msgs) -> Command;

// using ParseFunc = Command (*)(MessageArray msgs);
// static const std::unordered_map<std::string_view, ParseFunc, utils::string_hash, std::equal_to<>> ParseFuncs = {
//     {AppendCmd::Name, parse<AppendCmd>},
//     {DecrCmd::Name, parse<DecrCmd>},
//     {DecrByCmd::Name, parse<DecrByCmd>},
//     {GetCmd::Name, parse<GetCmd>},
//     {GetDelCmd::Name, parse<GetDelCmd>},
//     {GetRangeCmd::Name, parse<GetRangeCmd>},
//     {GetSetCmd::Name, parse<GetSetCmd>},
//     {IncrCmd::Name, parse<IncrCmd>},
//     {IncrByCmd::Name, parse<IncrByCmd>},
//     {SetCmd::Name, parse<SetCmd>},
//     {SetRangeCmd::Name, parse<SetRangeCmd>},
//     {StrLenCmd::Name, parse<StrLenCmd>}};

// static auto parse_command(MessageArray msgs) -> Command {
//   if (!msgs || msgs->empty()) {
//     throw ExecutionException{"EMPTY_INPUT cannot execute null or empty command"};
//   }

//   auto cmd_name = get_str((*msgs)[0]);
//   auto it = ParseFuncs.find(cmd_name);
//   if (it == ParseFuncs.end()) {
//     throw ExecutionException{"UNKNOWN_COMMAND"};
//   }
//   return it->second(std::move(msgs));
// }

// auto Execute(DB & /*db*/, Client & /*client*/, Message query) -> Message try {
// } catch (ExecutionException &e) {
//   return SingularMessage{ErrorMessage{e.error}};
// }

auto reply(resp::Serializer &resp_sender, redispp::resp::Channel &ch) -> boost::asio::awaitable<void> {
  while (true) {
    auto tok = co_await ch.async_receive(boost::asio::use_awaitable);
    co_await resp_sender.Serialize(tok);
    if (std::holds_alternative<resp::EndOfCommand_t>(tok)) {
      break;
    }
  }
}

auto Execute(DB & /*db*/,
             Client & /*client*/,
             resp::Deserializer &query_reader,
             resp::Serializer &resp_sender,
             redispp::resp::Channel &ch) -> boost::asio::awaitable<void> {
  co_await (query_reader.SendTokens(ch) && reply(resp_sender, ch));
}

}  // namespace redispp