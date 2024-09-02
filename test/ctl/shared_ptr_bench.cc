// -*- mode:c++; indent-tabs-mode:nil; c-basic-offset:4; coding:utf-8 -*-
// vi: set et ft=cpp ts=4 sts=4 sw=4 fenc=utf-8 :vi
//
// Copyright 2024 Justine Alexandra Roberts Tunney
//
// Permission to use, copy, modify, and/or distribute this software for
// any purpose with or without fee is hereby granted, provided that the
// above copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
// WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
// DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
// PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
// TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
// PERFORMANCE OF THIS SOFTWARE.

#include "ctl/shared_ptr.h"
#include "libc/dce.h"
#include "libc/mem/leaks.h"
#include "libc/testlib/benchmark.h"

#include "libc/calls/struct/timespec.h"
#include "libc/runtime/runtime.h"
#include "libc/stdio/stdio.h"
#include "libc/str/str.h"

using ctl::make_shared;
using ctl::shared_ptr;
using ctl::weak_ptr;

using null_deleter = decltype([](auto){});

#if IsModeDbg()
#define ITERATIONS 1000 // because qemu in dbg mode is very slow
#else
#define ITERATIONS 1000000
#endif

int
main()
{
  const auto x_shared = make_shared<int>();

  BENCHMARK(ITERATIONS, 1, {
    shared_ptr<int> y(new int());
  });

  BENCHMARK(ITERATIONS, 1, {
    shared_ptr<int> y(new int());
    shared_ptr<int> x(y);
  });

  BENCHMARK(ITERATIONS, 1, {
    shared_ptr<int> y(new int());
    shared_ptr<int> x(ctl::move(y));
  });

  BENCHMARK(ITERATIONS, 1, {
    shared_ptr<int> y(x_shared);
  });

  BENCHMARK(ITERATIONS, 1, {
    shared_ptr<int> y(new int());
    weak_ptr<int> x(y);
  });

  return 0;
}
