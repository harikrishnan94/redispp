///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Lewis Baker
// Licenced under MIT license. See LICENSE.txt for details.
///////////////////////////////////////////////////////////////////////////////
#ifndef CPPCORO_GENERATOR_HPP_INCLUDED
#define CPPCORO_GENERATOR_HPP_INCLUDED

#include <coroutine>
#include <exception>
#include <functional>
#include <iterator>
#include <type_traits>
#include <utility>

namespace redispp::utils {
template <typename T>
class generator;

namespace detail {
template <typename T>
class generator_promise {
 public:
  using value_type = std::remove_reference_t<T>;
  using reference_type = std::conditional_t<std::is_reference_v<T>, T, T&>;
  using pointer_type = value_type*;

  generator_promise() = default;

  auto get_return_object() noexcept -> generator<T>;

  [[nodiscard]] constexpr auto initial_suspend() const noexcept -> std::suspend_always { return {}; }
  [[nodiscard]] constexpr auto final_suspend() const noexcept -> std::suspend_always { return {}; }

  template <typename U = T, std::enable_if_t<!std::is_rvalue_reference<U>::value, int> = 0>
  auto yield_value(std::remove_reference_t<T>& value) noexcept -> std::suspend_always {
    m_value = std::addressof(value);
    return {};
  }

  auto yield_value(std::remove_reference_t<T>&& value) noexcept -> std::suspend_always {
    m_value = std::addressof(value);
    return {};
  }

  void unhandled_exception() { m_exception = std::current_exception(); }

  void return_void() {}

  auto value() const noexcept -> reference_type { return static_cast<reference_type>(*m_value); }

  // Don't allow any use of 'co_await' inside the generator coroutine.
  template <typename U>
  auto await_transform(U&& value) -> std::suspend_never = delete;

  void rethrow_if_exception() {
    if (m_exception) {
      std::rethrow_exception(m_exception);
    }
  }

 private:
  pointer_type m_value;
  std::exception_ptr m_exception;
};

struct generator_sentinel {};

template <typename T>
class generator_iterator {
  using coroutine_handle = std::coroutine_handle<generator_promise<T>>;

 public:
  using iterator_category = std::input_iterator_tag;
  // What type should we use for counting elements of a potentially infinite sequence?
  using difference_type = std::ptrdiff_t;
  using value_type = typename generator_promise<T>::value_type;
  using reference = typename generator_promise<T>::reference_type;
  using pointer = typename generator_promise<T>::pointer_type;

  // Iterator needs to be default-constructible to satisfy the Range concept.
  generator_iterator() noexcept : m_coroutine(nullptr) {}

  explicit generator_iterator(coroutine_handle coroutine) noexcept : m_coroutine(coroutine) {}

  friend auto operator==(const generator_iterator& it, generator_sentinel /*unused*/) noexcept -> bool {
    return !it.m_coroutine || it.m_coroutine.done();
  }

  friend auto operator!=(const generator_iterator& it, generator_sentinel s) noexcept -> bool { return !(it == s); }

  friend auto operator==(generator_sentinel s, const generator_iterator& it) noexcept -> bool { return (it == s); }

  friend auto operator!=(generator_sentinel s, const generator_iterator& it) noexcept -> bool { return it != s; }

  auto operator++() -> generator_iterator& {
    m_coroutine.resume();
    if (m_coroutine.done()) {
      m_coroutine.promise().rethrow_if_exception();
    }

    return *this;
  }

  // Need to provide post-increment operator to implement the 'Range' concept.
  void operator++(int) { (void)operator++(); }

  auto operator*() const noexcept -> reference { return m_coroutine.promise().value(); }

  auto operator->() const noexcept -> pointer { return std::addressof(operator*()); }

 private:
  coroutine_handle m_coroutine;
};
}  // namespace detail

template <typename T>
class [[nodiscard]] generator {  // NOLINT(*-special-member-functions)
 public:
  using promise_type = detail::generator_promise<T>;
  using iterator = detail::generator_iterator<T>;

  generator() noexcept : m_coroutine(nullptr) {}

  generator(generator&& other) noexcept : m_coroutine(other.m_coroutine) { other.m_coroutine = nullptr; }

  generator(const generator& other) = delete;

  ~generator() {
    if (m_coroutine) {
      m_coroutine.destroy();
    }
  }

  auto operator=(generator other) noexcept -> generator& {
    swap(other);
    return *this;
  }

  auto begin() -> iterator {
    if (m_coroutine) {
      m_coroutine.resume();
      if (m_coroutine.done()) {
        m_coroutine.promise().rethrow_if_exception();
      }
    }

    return iterator{m_coroutine};
  }

  auto end() noexcept -> detail::generator_sentinel { return detail::generator_sentinel{}; }

  void swap(generator& other) noexcept { std::swap(m_coroutine, other.m_coroutine); }

 private:
  friend class detail::generator_promise<T>;

  explicit generator(std::coroutine_handle<promise_type> coroutine) noexcept : m_coroutine(coroutine) {}

  std::coroutine_handle<promise_type> m_coroutine;
};

template <typename T>
void swap(generator<T>& a, generator<T>& b) {
  a.swap(b);
}

namespace detail {
template <typename T>
auto generator_promise<T>::get_return_object() noexcept -> generator<T> {
  using coroutine_handle = std::coroutine_handle<generator_promise<T>>;
  return generator<T>{coroutine_handle::from_promise(*this)};
}
}  // namespace detail

template <typename FUNC, typename T>
auto fmap(FUNC func, generator<T> source)
    -> generator<std::invoke_result_t<FUNC&, typename generator<T>::iterator::reference>> {
  for (auto&& value : source) {
    co_yield std::invoke(func, static_cast<decltype(value)>(value));
  }
}
}  // namespace redispp::utils

#endif