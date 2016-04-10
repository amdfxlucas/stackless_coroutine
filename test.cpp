// Copyright 2016 John R. Bandela
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "stackless_coroutine.hpp"
#include <thread>
#define CATCH_CONFIG_MAIN
#include "catch.hpp"
#include <functional>
#include <future>
#include <memory>
#include <utility>

template <class F> void do_thread(F f) {
  std::thread t{[f]() mutable { f(); }};
  t.detach();
}

template <class R> struct value_t {
  R return_value;
  std::promise<R> p;
};

template <class V, class T> auto get_future(T t) {
  auto p = std::make_unique<V>();
  auto fut = p->p.get_future();
  stackless_coroutine::run(std::move(p), t,
                           [](V &value, std::exception_ptr ep, bool async,
                              stackless_coroutine::operation op) {
                             if (ep) {
                               value.p.set_exception(ep);
                             } else {
                               value.p.set_value(value.return_value);
                             }
                           });
  return fut;
}

TEST_CASE("Simple test", "[stackless]") {
  auto f = get_future<value_t<int>>(stackless_coroutine::make_block(
      [](auto &context, auto &value) { value.return_value = 1; }));
  REQUIRE(f.get() == 1);
}

TEST_CASE("Simple async", "[stackless]") {
  auto f = get_future<value_t<int>>(stackless_coroutine::make_block(
      [](auto &context, auto &value) {
        value.return_value = 1;
        do_thread([context]() mutable { context(1); });
        return context.do_async();
      },
      [](auto &context, auto &value, int aval) {
        value.return_value += aval;
      }));
  REQUIRE(f.get() == 2);
}

TEST_CASE("while async", "[stackless]") {
  auto f = get_future<value_t<int>>(stackless_coroutine::make_block(
      [](auto &context, auto &value) { value.return_value = 1; },
      stackless_coroutine::make_while_true(
          [](auto &context, auto &value) {
            if (value.return_value < 5)
              return context.do_next();
            else
              return context.do_break();

          },
          [](auto &context, auto &value) {
            do_thread([context]() mutable { context(1); });
            return context.do_async();
          },
          [](auto &context, auto &value, int aval) {
            value.return_value += aval;
          })));
  REQUIRE(f.get() == 5);
}

TEST_CASE("while if async", "[stackless]") {
  auto f = get_future<value_t<int>>(stackless_coroutine::make_block(
      [](auto &context, auto &value) { value.return_value = 1; },
      stackless_coroutine::make_while_true(
          stackless_coroutine::make_if(
              [](auto &value) { return value.return_value < 5; },
              stackless_coroutine::make_block(
                  [](auto &context, auto &value) { return context.do_next(); }),
              stackless_coroutine::make_block([](auto &context, auto &value) {
                return context.do_break();
              })),
          [](auto &context, auto &value) {
            do_thread([context]() mutable { context(1); });
            return context.do_async();
          },
          [](auto &context, auto &value, int aval) {
            value.return_value += aval;
          })));
  REQUIRE(f.get() == 5);
}

template <class R> struct value_temp_t {
  R return_value;
  R inner;
  R outer;
  std::promise<R> p;
};
TEST_CASE("inner and outer while if async", "[stackless]") {
  auto f = get_future<value_temp_t<int>>(stackless_coroutine::make_block(
      [](auto &context, auto &value) {
        value.return_value = 0;
        value.inner = 0;
        value.outer = 0;
      },
      stackless_coroutine::make_while_true(
          stackless_coroutine::make_if(
              [](auto &value) { return value.outer < 5; },
              stackless_coroutine::make_block(
                  [](auto &context, auto &value) { return context.do_next(); }),
              stackless_coroutine::make_block([](auto &context, auto &value) {
                return context.do_break();
              })),
          [](auto &context, auto &value) {
            do_thread([context]() mutable { context(1); });
            return context.do_async();
          },
          [](auto &context, auto &value, int aval) {
            value.outer += aval;
            value.inner = 0;
          },
          stackless_coroutine::make_while_true(
              stackless_coroutine::make_if(
                  [](auto &value) { return value.inner < 7; },
                  stackless_coroutine::make_block(
                      [](auto &context, auto &value) {
                        return context.do_next();
                      }),
                  stackless_coroutine::make_block(
                      [](auto &context, auto &value) {
                        return context.do_break();
                      })),
              [](auto &context, auto &value) {
                do_thread([context]() mutable { context(1); });
                return context.do_async();
              },
              [](auto &context, auto &value, int aval) {
                value.return_value += aval;
                value.inner += aval;
              }))));
  REQUIRE(f.get() == 35);
}
TEST_CASE("inner and outer while if async with early return", "[stackless]") {
  auto f = get_future<value_temp_t<int>>(stackless_coroutine::make_block(
      [](auto &context, auto &value) {
        value.return_value = 0;
        value.inner = 0;
        value.outer = 0;
      },
      stackless_coroutine::make_while_true(
          stackless_coroutine::make_if(
              [](auto &value) { return value.outer < 5; },
              stackless_coroutine::make_block(
                  [](auto &context, auto &value) { return context.do_next(); }),
              stackless_coroutine::make_block([](auto &context, auto &value) {
                return context.do_break();
              })),
          [](auto &context, auto &value) {
            do_thread([context]() mutable { context(1); });
            return context.do_async();
          },
          [](auto &context, auto &value, int aval) {
            value.outer += aval;
            value.inner = 0;
          },
          stackless_coroutine::make_while_true(
              stackless_coroutine::make_if(
                  [](auto &value) { return value.inner < 7; },
                  stackless_coroutine::make_block(
                      [](auto &context, auto &value) {
                        return context.do_next();
                      }),
                  stackless_coroutine::make_block(
                      [](auto &context, auto &value) {
                        return context.do_break();
                      })),
              [](auto &context, auto &value) {
                do_thread([context]() mutable { context(1); });
                return context.do_async();
              },
              [](auto &context, auto &value, int aval) {
                value.return_value += aval;
                value.inner += aval;
              },
              stackless_coroutine::make_if(
                  [](auto &value) { return value.return_value == 29; },
                  stackless_coroutine::make_block(
                      [](auto &context, auto &value) {
                        return context.do_return();
                      }),
                  stackless_coroutine::make_block(
                      [](auto &context, auto &value) {})

                      )))));
  REQUIRE(f.get() == 29);
}
TEST_CASE("inner and outer while if async with early return using make_while "
          "instead of make_while_true",
          "[stackless]") {
  auto f = get_future<value_temp_t<int>>(stackless_coroutine::make_block(
      [](auto &context, auto &value) {
        value.return_value = 0;
        value.inner = 0;
        value.outer = 0;
      },
      stackless_coroutine::make_while(
          [](auto &value) { return value.outer < 5; },
          [](auto &context, auto &value) {
            do_thread([context]() mutable { context(1); });
            return context.do_async();
          },
          [](auto &context, auto &value, int aval) {
            value.outer += aval;
            value.inner = 0;
          },
          stackless_coroutine::make_while(
              [](auto &value) { return value.inner < 7; },
              [](auto &context, auto &value) {
                do_thread([context]() mutable { context(1); });
                return context.do_async();
              },
              [](auto &context, auto &value, int aval) {
                value.return_value += aval;
                value.inner += aval;
              },
              stackless_coroutine::make_if(
                  [](auto &value) { return value.return_value == 29; },
                  stackless_coroutine::make_block(
                      [](auto &context, auto &value) {
                        return context.do_return();
                      }),
                  stackless_coroutine::make_block(
                      [](auto &context, auto &value) {})

                      )))));
  REQUIRE(f.get() == 29);
}

TEST_CASE("inner and outer while if async with early return using make_while "
          "instead of make_while_true, but use do_async_break",
          "[stackless]") {
  auto f = get_future<value_temp_t<int>>(stackless_coroutine::make_block(
      [](auto &context, auto &value) {
        value.return_value = 0;
        value.inner = 0;
        value.outer = 0;
      },
      stackless_coroutine::make_while(
          [](auto &value) { return value.outer < 5; },
          [](auto &context, auto &value) {
            do_thread([context]() mutable { context(1); });
            return context.do_async();
          },
          [](auto &context, auto &value, int aval) {
            value.outer += aval;
            value.inner = 0;
          },
          stackless_coroutine::make_while_true(
              [](auto &context, auto &value) {
                if (value.inner < 7) {
                  do_thread([context]() mutable { context(1); });
                  return context.do_async();
                } else {
                  return context.do_async_break();
                }
              },
              [](auto &context, auto &value, int aval) {
                value.return_value += aval;
                value.inner += aval;
              },
              stackless_coroutine::make_if(
                  [](auto &value) { return value.return_value == 29; },
                  stackless_coroutine::make_block(
                      [](auto &context, auto &value) {
                        return context.do_return();
                      }),
                  stackless_coroutine::make_block(
                      [](auto &context, auto &value) {})

                      )))));
  REQUIRE(f.get() == 29);
}

TEST_CASE("inner and outer while if async with early return using exception using make_while "
	"instead of make_while_true, but use do_async_break",
	"[stackless]") {

	int val = -1;
	auto f = get_future<value_temp_t<int>>(stackless_coroutine::make_block(
		[](auto &context, auto &value) {
		value.return_value = 0;
		value.inner = 0;
		value.outer = 0;
	},
		stackless_coroutine::make_while(
			[](auto &value) { return value.outer < 5; },
			[](auto &context, auto &value) {
		do_thread([context]() mutable { context(1); });
		return context.do_async();
	},
			[](auto &context, auto &value, int aval) {
		value.outer += aval;
		value.inner = 0;
	},
		stackless_coroutine::make_while_true(
			[](auto &context, auto &value) {
		if (value.inner < 7) {
			do_thread([context]() mutable { context(1); });
			return context.do_async();
		}
		else {
			return context.do_async_break();
		}
	},
			[](auto &context, auto &value, int aval) {
		value.return_value += aval;
		value.inner += aval;
	},
		stackless_coroutine::make_if(
			[](auto &value) { return value.return_value == 29; },
			stackless_coroutine::make_block(
				[](auto &context, auto &value) {
				
		std::string message = "Exception thrown. value: " + std::to_string(value.return_value);
		throw std::runtime_error(message);
	}),
			stackless_coroutine::make_block(
				[](auto &context, auto &value) {})

		)))));
	try {
		f.get();
	}
	catch (std::exception& e) {

		REQUIRE(e.what() == std::string("Exception thrown. value: 29"));
		return;
	}
	REQUIRE(1 == 2);

}
