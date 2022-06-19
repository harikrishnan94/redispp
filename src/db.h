#pragma once

#include <cstdint>
#include <memory_resource>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "exec.h"
#include "string_hash.h"

namespace redispp {
class DB;
class Executor;

using Transaction = std::vector<exec::Command>;
using ClientID = uint64_t;

class Client {
 private:
  friend class Executor;

  void AddQueryToCurTxn(exec::Command query) { m_cur_txn.push_back(std::move(query)); }

  auto GetAndClearCurTxn() noexcept -> Transaction { return std::exchange(m_cur_txn, {}); }

  ClientID m_id;
  DB *m_db = nullptr;
  Transaction m_cur_txn = {};
};

class DB {
 public:
  explicit DB(std::pmr::memory_resource &alloc = *std::pmr::get_default_resource()) : m_alloc(&alloc) {}

  auto Get(std::string_view key) const noexcept -> std::optional<std::string_view> {
    auto it = m_key_vals.find(key);
    if (it == m_key_vals.end()) {
      return {};
    }
    return it->second;
  }

  auto Get(std::string_view key) noexcept -> std::pmr::string * {
    auto it = m_key_vals.find(key);
    if (it == m_key_vals.end()) {
      return nullptr;
    }
    return &it->second;
  }

  auto GetAndSet(std::pmr::string key, std::pmr::string value) -> std::optional<std::pmr::string> {
    auto it = m_key_vals.find(key);
    if (it == m_key_vals.end()) {
      m_key_vals[std::move(key)] = std::move(value);
      return {};
    }
    return std::exchange(it->second, std::move(value));
  }

  auto GetAndSet(std::string_view key, std::pmr::string value) -> std::optional<std::pmr::string> {
    auto it = m_key_vals.find(key);
    if (it == m_key_vals.end()) {
      m_key_vals[key.data()] = std::move(value);
      return {};
    }
    return std::exchange(it->second, std::move(value));
  }

  auto Delete(std::string_view key) -> std::optional<std::pmr::string> {
    auto it = m_key_vals.find(key);
    if (it == m_key_vals.end()) {
      return {};
    }

    auto ret = std::move(it->second);
    m_key_vals.erase(it);
    return ret;
  }

 private:
  std::pmr::memory_resource *m_alloc;
  std::pmr::unordered_map<std::pmr::string, std::pmr::string, utils::string_hash, std::equal_to<>> m_key_vals{m_alloc};
};
}  // namespace redispp