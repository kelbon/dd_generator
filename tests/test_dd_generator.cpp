
#include <dd_generator.hpp>
#include <string>
#include <memory_resource>
#include <iostream>

struct check {
  bool b = false;
  check(int, float) : b(true) {
  }
  check(int) : b(false) {
  }
};
dd::generator<check> gen(std::size_t i) {
  for (int j = 0; j < i; ++j)
    co_yield {5, 5.f};
}
dd::generator<std::string> gen1(std::allocator_arg_t, std::pmr::polymorphic_allocator<std::byte> r, std::size_t i) {
  for (int j = 0; j < i; ++j)
    co_yield "hello world";
}

dd::generator<int> gen2(int n) {
  while (true) {
    ++n;
    co_yield n;
  }
}
int main() {
  for (auto s : gen(15))
    if (s.b != true)
      return -1;
  for (std::string& s : gen1(std::allocator_arg, std::pmr::new_delete_resource(), 10))
    if (s != "hello world")
      return -2;
  auto g = gen2(10);
  for (int j = 0; int& i : g) {
    std::cout << i << '\n';
    ++i;  // access to generator value by ref
    if (j++ >= 10)
      break;
  }
  dd::generator r = std::move(g);
  assert(g.empty());
  for (int j = 0; int& i : r) {
    std::cout << i << '\n';
    ++i; // access to generator value by ref
    if (j++ >= 10)
      break;
  }
  auto gg = []() -> dd::generator<int> {
    for (int i = 0; i < 20; ++i)
      co_yield i;
  }();
  // UB with std::generator...
  for (auto i : gg) {
    std::cout << i << '\n';
    if (i == 10)
      break;
  }
  for (auto i : gg)
    std::cout << i << '\n';
  static_assert(std::is_same_v<
                std::coroutine_traits<dd::generator<std::string>, std::allocator_arg_t,
                                      std::pmr::polymorphic_allocator<std::byte>, std::size_t>::promise_type,
                dd::generator_promise<std::string, std::pmr::polymorphic_allocator<std::byte>>>);
}