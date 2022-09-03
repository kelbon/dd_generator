#pragma once

#ifndef DD_GENERATOR
#define DD_GENERATOR

#ifndef DD_MODULE_EXPORT
#define DD_MODULE_EXPORT

#include <cassert>
#include <coroutine>
#include <iterator>
#include <memory>
#include <utility>
#endif
// compilers do to support now
#define consteval constexpr

namespace dd {

struct input_and_output_iterator_tag : std::input_iterator_tag, std::output_iterator_tag {};

template <typename Yield, typename Alloc>
struct generator_promise {
  Yield* current_result = nullptr;

  // leading allocator convention
  template <typename... Args>
  static void* operator new(std::size_t frame_size, std::allocator_arg_t, Alloc resource, Args&&...) {
    static_assert(std::is_same_v<std::byte, typename Alloc::value_type> &&
                  std::is_same_v<typename std::allocator_traits<Alloc>::pointer, std::byte*>);
    // check for default here, because need to create it by default in operator delete
    if constexpr (std::is_empty_v<Alloc> && std::default_initializable<Alloc>) {
      return resource.allocate(frame_size);
    } else {
      // What coroutine frame alignment??? Where allocator::allocate overload with alignment?
      // Atleast for specialization allocator<std::byte>?
      // Why it is operator new/delete and not promise_type::allocate_frame(size_t, std::align_t, Args&&...) ?
      // Why overloading operator new for promise_type used NOT for promise_type ?! WTF?
      // Why commitee want to ALLOCATOR support for coroutines, when its really dont need to be typed
      // and its just memory resource for exactly one allocation and deallocation? It must not be an
      // allocator!
      // Why there are no way to use exactly same alloc, which is in coroutine frame?(need to create copy)
      std::byte* frame_ptr = reinterpret_cast<std::byte*>(resource.allocate(frame_size + sizeof(Alloc)));
      std::construct_at(reinterpret_cast<Alloc*>(frame_ptr + frame_size), std::move(resource));
      return frame_ptr;
    }
  }
  static void* operator new(std::size_t frame_size) requires(std::default_initializable<Alloc>) {
    Alloc a{};  // its empty, must be legal
    return operator new(frame_size, std::allocator_arg, a);
  }

  static void operator delete(void* ptr, std::size_t frame_size) noexcept {
    auto* p = reinterpret_cast<std::byte*>(ptr);
    if constexpr (std::is_empty_v<Alloc> && std::default_initializable<Alloc>) {
      Alloc{}.deallocate(p, frame_size);
    } else {  // Fuck aligment again
      auto* resource_on_frame = reinterpret_cast<Alloc*>(p + frame_size);
      // move it from frame, because its in memory which it will deallocate
      auto resource = std::move(*resource_on_frame);
      std::destroy_at(resource_on_frame);
      resource.deallocate(p, frame_size + sizeof(Alloc));
    }
  }

  static consteval std::suspend_always initial_suspend() noexcept {
    return {};
  }
  static consteval std::suspend_always final_suspend() noexcept {
    return {};
  }
  static consteval void return_void() noexcept {
  }
  auto get_return_object() {
    return std::coroutine_handle<generator_promise>::from_promise(*this);
  }

  // yield things
 private:
  struct save_value_before_resume_t {
    Yield saved_value;

    // ctor for emplace without copy
    template <typename T>
    save_value_before_resume_t(T&& value) noexcept(std::is_nothrow_constructible_v<Yield, T&&>)
        : saved_value(std::forward<T>(value)) {
    }
    static consteval bool await_ready() noexcept {
      return false;
    }
    void await_suspend(std::coroutine_handle<generator_promise<Yield, Alloc>> handle) noexcept {
      handle.promise().current_result = std::addressof(saved_value);
    }
    static consteval void await_resume() noexcept {
    }
  };

 public:
  // lvalue
  std::suspend_always yield_value(Yield& lvalue) noexcept {
    current_result = std::addressof(lvalue);
    return {};
  }
  // rvalue or some type which is convertible to Yield
  template <typename U = Yield>
  auto yield_value(U&& value) noexcept(std::is_nothrow_constructible_v<Yield, U&&>) {
    return save_value_before_resume_t{std::forward<U>(value)};
  }

  [[noreturn]] static void unhandled_exception() {
    throw;
  }
};

// dont know why, but coroutine_handle<void> do not have .promise() -> void* method
// but it is possible to implement without overhead(msvc realization :
// return *reinterpret_cast<_Promise*>(__builtin_coro_promise(_Ptr, 0, false));
// clang version requires alignment of promise,
// but it is possible to do promise(size_t align) in specialization coroutine_handle<void>
struct any_coroutine_handle {
 private:
  std::coroutine_handle<void> handle_ = nullptr;
  void* promise_ptr_ = nullptr;
 public:
  constexpr any_coroutine_handle() = default;

  template <typename P>
  any_coroutine_handle(std::coroutine_handle<P> handle) noexcept
      : handle_(handle), promise_ptr_(std::addressof(handle.promise())) {
  }
  any_coroutine_handle(std::nullptr_t) noexcept : handle_(nullptr), promise_ptr_(nullptr) {
  }
  void resume() const {
    assert(handle_ != nullptr);
    handle_.resume();
  }
  void destroy() const noexcept {
    assert(handle_ != nullptr);
    handle_.destroy();
  }
  bool done() const noexcept {
    assert(handle_ != nullptr);
    return handle_.done();
  }
  // precondition - *this != nullptr
  void* promise() const noexcept {
    assert(handle_ != nullptr);
    return promise_ptr_;
  }
  bool operator==(std::nullptr_t) const noexcept {
    return handle_ == nullptr;
  }
};

// synchronous producer
DD_MODULE_EXPORT template <typename Yield>
struct generator {
 public:
  using value_type = Yield;

 private:
  // INVARIANT - all promise types has value_type* as FIRST field and standard layout
  // (so pointer to first field may be converted to pointer to value)
  // TODO it must be one field(coroutine_handle<void>), but now i cant restore handle from promise
  // address(without promise type) we need coroutine_handle<void>::promise() which will return void*
  any_coroutine_handle handle_ = nullptr;

 public:
  constexpr generator() noexcept = default;
  template <typename Alloc>
  constexpr generator(std::coroutine_handle<generator_promise<Yield, Alloc>> handle) noexcept
      : handle_(handle) {
  }
  constexpr generator(generator&& other) noexcept
      : handle_(std::exchange(other.handle_, nullptr)) {
  }
  constexpr generator& operator=(generator&& other) noexcept {
    using std::swap;
    swap(handle_, other.handle_);
    return *this;
  }

  ~generator() {
    if (handle_ != nullptr)
      handle_.destroy();
  }

  struct iterator {
    any_coroutine_handle owner_;

    using iterator_category = input_and_output_iterator_tag;
    using value_type = Yield;
    using difference_type = ptrdiff_t;

    bool operator==(std::default_sentinel_t) const noexcept {
      return owner_.done();
    }
    value_type& operator*() const noexcept {
      // Invariant of generator, that promise always have Yield* as a first field and starts with it
      return **reinterpret_cast<Yield**>(owner_.promise());
    }
    iterator& operator++() {
      owner_.resume();
      return *this;
    }
    iterator operator++(int) {
      return ++(*this);
    }
  };
  // clang-format off
  [[nodiscard("generator::begin has side effects!")]]
  iterator begin() {
    // clang-format on
    assert(!empty());
    handle_.resume();
    return iterator{handle_};
  }

  static consteval std::default_sentinel_t end() noexcept {
    return std::default_sentinel;
  }

  // observers
  bool empty() const noexcept {
    return handle_ == nullptr || handle_.done();
  }
};

}  // namespace dd

namespace std {

// default case
DD_MODULE_EXPORT template <typename Yield, typename... Ts>
struct coroutine_traits<::dd::generator<Yield>, Ts...> {
  using promise_type = ::dd::generator_promise<Yield, std::allocator<std::byte>>;
};

DD_MODULE_EXPORT template <typename Yield, typename Alloc, typename... Ts>
struct coroutine_traits<::dd::generator<Yield>, ::std::allocator_arg_t, Alloc, Ts...> {
  using promise_type = ::dd::generator_promise<Yield, Alloc>;
};

// method case
// clang-format off
DD_MODULE_EXPORT template <typename Yield, typename Type, typename Alloc, typename... Ts>
requires(std::is_class_v<Type>)
struct coroutine_traits<::dd::generator<Yield>, Type, ::std::allocator_arg_t, Alloc, Ts...> {
  using promise_type = ::dd::generator_promise<Yield, Alloc>;
};
// clang-format on

}  // namespace std

#endif // !DD_GENERATOR