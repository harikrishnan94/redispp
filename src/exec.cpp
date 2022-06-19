#include "exec.h"

#include <charconv>
#include <cstddef>
#include <exception>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <variant>

#include "message.h"

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

// struct AppendCmd {
//   Str key;
//   Str val;

//   static constexpr std::string_view Name = "APPEND";
// };

// struct DecrCmd {
//   Str key;

//   static constexpr std::string_view Name = "DECR";
// };

// struct DecrByCmd {
//   Str key;
//   Integer val;

//   static constexpr std::string_view Name = "DECRBY";
// };

// struct GetCmd {
//   Str key;

//   static constexpr std::string_view Name = "GET";
// };

// struct GetDelCmd {
//   Str key;

//   static constexpr std::string_view Name = "GETDEL";
// };

// struct GetRangeCmd {
//   Str key;
//   size_t start;
//   size_t end;

//   static constexpr std::string_view Name = "GETRANGE";
// };

// struct GetSetCmd {
//   Str key;
//   Str val;

//   static constexpr std::string_view Name = "GETSET";
// };

// struct IncrCmd {
//   Str key;

//   static constexpr std::string_view Name = "INCR";
// };

// struct IncrByCmd {
//   Str key;
//   Integer val;

//   static constexpr std::string_view Name = "INCRBY";
// };

// struct SetCmd {
//   Str key;
//   Str val;

//   static constexpr std::string_view Name = "SET";
// };

// struct SetRangeCmd {
//   Str key;
//   size_t offset;
//   Str val;

//   static constexpr std::string_view Name = "SETRANGE";
// };

// struct StrLenCmd {
//   Str key;

//   static constexpr std::string_view Name = "STRLEN";
// };

// using Command = std::variant<AppendCmd,
//                              DecrCmd,
//                              DecrByCmd,
//                              GetCmd,
//                              GetDelCmd,
//                              GetRangeCmd,
//                              GetSetCmd,
//                              IncrCmd,
//                              IncrByCmd,
//                              SetCmd,
//                              SetRangeCmd,
//                              StrLenCmd>;

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
auto Execute(DB& /*db*/, Client& /*client*/, Message) -> Message { return {}; }
}  // namespace redispp