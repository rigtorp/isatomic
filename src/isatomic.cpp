// © 2020 Erik Rigtorp <erik@rigtorp.se>
// SPDX-License-Identifier: MIT

#include <atomic>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

int main(int argc, char *argv[]) {

  using namespace std::string_literals;

  size_t iters = 1000000;
  enum {
    ALIGNED128,
    UNALIGNED128,
    SPLIT128,
    ALIGNED256,
    UNALIGNED256,
    SPLIT256,
    ALIGNED512,
    SPLIT512,
    NONE
  } type = NONE;

  int opt;
  while ((opt = getopt(argc, argv, "i:t:")) != -1) {
    switch (opt) {
    case 'i':
      iters = std::stoi(optarg);
      break;
    case 't':
      if (optarg == "128"s) {
        type = ALIGNED128;
        break;
      }
      if (optarg == "128u"s) {
        type = UNALIGNED128;
        break;
      }
      if (optarg == "128s"s) {
        type = SPLIT128;
        break;
      }
      if (optarg == "256"s) {
        type = ALIGNED256;
        break;
      }
      if (optarg == "256u"s) {
        type = UNALIGNED256;
        break;
      }
      if (optarg == "256s"s) {
        type = SPLIT256;
        break;
      }
      if (optarg == "512"s) {
        type = ALIGNED512;
        break;
      }
      if (optarg == "512s"s) {
        type = SPLIT512;
        break;
      }
      goto usage;
    default:
      goto usage;
    }
  }

  if (type == NONE) {
    std::cerr << "must specify test type (-t)!" << std::endl;
    goto usage;
  }

  if (optind != argc) {
  usage:
    std::cerr << "isatomic 1.0.0 © 2020 Erik Rigtorp <erik@rigtorp.se>\n"
                 "usage: isatomic [-i iters] -t 128|128u|128s|256|256u|256s\n"
                 "tests if 16B/32B wide loads/stores are atomic\n"
                 "number of iterations defaults to 1000000\n"
                 "128:  16B loads/stores\n"
                 "128u: 16B unaligned loads/stores\n"
                 "128s: 16B cacheline split loads/stores\n"
                 "256:  32B loads/stores\n"
                 "256u: 32B unaligned loads/stores\n"
                 "256s: 32B cacheline split loads/stores\n"
                 "512:  64B aligned loads/stores\n"
                 "512s: 64B cacheline split loads/stores\n"
                 "returns 1 if any torn reads were detected"
              << std::endl;
    exit(1);
  }

  cpu_set_t set;
  CPU_ZERO(&set);
  if (sched_getaffinity(0, sizeof(set), &set) == -1) {
    perror("sched_getaffinity");
    exit(1);
  }

  // enumerate available CPUs
  std::vector<int> cpus;
  for (int i = 0; i < CPU_SETSIZE; ++i) {
    if (CPU_ISSET(i, &set)) {
      cpus.push_back(i);
    }
  }

  alignas(64) char buf[128] = {};

  std::array<std::atomic<size_t>, 256> counts = {};
  std::atomic<size_t> active_threads = {0};
  auto func = [&](int cpu) {
    // pin current thread to assigned CPU
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof(set), &set) == -1) {
      perror("sched_setaffinity");
      exit(1);
    }

    std::array<size_t, 256> tcounts = {};

    // wait for all threads to be ready
    active_threads.fetch_add(1, std::memory_order_relaxed);
    while (active_threads.load(std::memory_order_relaxed) != cpus.size())
      ;

    // Pseudo code:
    // - Load 128b/256b of data
    // - Save bitmask of 4x 32b/64b float signbits
    // - Store 4x 32b/64b floats of all zero or all negative one
    // - Keep tally of observed signbit bitmask patterns

    // Note: Must use hand rolled assembly, since intrinsic functions are
    // sometimes optimized where 256b operation is replaced by two 128b
    // operations.

    switch (type) {
    case ALIGNED128:
      for (size_t i = 0; i < iters; ++i) {
        int x;
        float y = i % 2 ? 0 : -1;
        asm("vmovdqa %3, %%xmm0;"
            "vmovmskps %%xmm0, %0;"
            "vmovd %2, %%xmm1;"
            "vbroadcastss %%xmm1, %%xmm2;"
            "vmovdqa %%xmm2, %1;"
            : "=r"(x), "=m"(buf[0])
            : "r"(y), "m"(buf[0])
            : "%xmm0", "%xmm1", "%xmm2");
        tcounts[x]++;
      }
      break;
    case UNALIGNED128:
      for (size_t i = 0; i < iters; ++i) {
        int x;
        float y = i % 2 ? 0 : -1;
        asm("vmovdqu %3, %%xmm0;"
            "vmovmskps %%xmm0, %0;"
            "vmovd %2, %%xmm1;"
            "vbroadcastss %%xmm1, %%xmm2;"
            "vmovdqu %%xmm2, %1;"
            : "=r"(x), "=m"(buf[3])
            : "r"(y), "m"(buf[3])
            : "%xmm0", "%xmm1", "%xmm2");
        tcounts[x]++;
      }
      break;
    case SPLIT128:
      for (size_t i = 0; i < iters; ++i) {
        int x;
        float y = i % 2 ? 0 : -1;
        asm("vmovdqu %3, %%xmm0;"
            "vmovmskps %%xmm0, %0;"
            "vmovd %2, %%xmm1;"
            "vbroadcastss %%xmm1, %%xmm2;"
            "vmovdqu %%xmm2, %1;"
            : "=r"(x), "=m"(buf[56])
            : "r"(y), "m"(buf[56])
            : "%xmm0", "%xmm1", "%xmm2");
        tcounts[x]++;
      }
      break;
    case ALIGNED256:
      for (size_t i = 0; i < iters; ++i) {
        int x;
        double y = i % 2 ? 0 : -1;
        asm("vmovdqa %3, %%ymm0;"
            "vmovmskpd %%ymm0, %0;"
            "vmovq %2, %%xmm1;"
            "vbroadcastsd %%xmm1, %%ymm2;"
            "vmovdqa %%ymm2, %1;"
            : "=r"(x), "=m"(buf[0])
            : "r"(y), "m"(buf[0])
            : "%ymm0", "%xmm1", "%ymm2");
        tcounts[x]++;
      }
      break;
    case UNALIGNED256:
      for (size_t i = 0; i < iters; ++i) {
        int x;
        double y = i % 2 ? 0 : -1;
        asm("vmovdqu %3, %%ymm0;"
            "vmovmskpd %%ymm0, %0;"
            "vmovq %2, %%xmm1;"
            "vbroadcastsd %%xmm1, %%ymm2;"
            "vmovdqu %%ymm2, %1;"
            : "=r"(x), "=m"(buf[3])
            : "r"(y), "m"(buf[3])
            : "%ymm0", "%xmm1", "%ymm2");
        tcounts[x]++;
      }
      break;
    case SPLIT256:
      for (size_t i = 0; i < iters; ++i) {
        int x;
        double y = i % 2 ? 0 : -1;
        asm("vmovdqu %3, %%ymm0;"
            "vmovmskpd %%ymm0, %0;"
            "vmovq %2, %%xmm1;"
            "vbroadcastsd %%xmm1, %%ymm2;"
            "vmovdqu %%ymm2, %1;"
            : "=r"(x), "=m"(buf[48])
            : "r"(y), "m"(buf[48])
            : "%ymm0", "%xmm1", "%ymm2");
        tcounts[x]++;
      }
      break;
    case ALIGNED512:
      for (size_t i = 0; i < iters; ++i) {
        int x;
        double y = i % 2 ? 0 : -1;
        asm("vmovdqa64 %3, %%zmm0;"
            "vpmovq2m %%zmm0, %%k1;"
            "kmovb %%k1, %0;"
            "vmovq %2, %%xmm1;"
            "vbroadcastsd %%xmm1, %%zmm2;"
            "vmovdqa64 %%zmm2, %1;"
            : "=r"(x), "=m"(buf[0])
            : "r"(y), "m"(buf[0])
            : "%zmm0", "%xmm1", "%zmm2", "%k1");
        tcounts[x]++;
      }
      break;
    case SPLIT512:
      for (size_t i = 0; i < iters; ++i) {
        int x;
        double y = i % 2 ? 0 : -1;
        asm("vmovdqu64 %3, %%zmm0;"
            "vpmovq2m %%zmm0, %%k1;"
            "kmovb %%k1, %0;"
            "vmovq %2, %%xmm1;"
            "vbroadcastsd %%xmm1, %%zmm2;"
            "vmovdqu64 %%zmm2, %1;"
            : "=r"(x), "=m"(buf[32])
            : "r"(y), "m"(buf[32])
            : "%zmm0", "%xmm1", "%zmm2", "%k1");
        tcounts[x]++;
      }
      break;
    case NONE:
      std::abort();
    }

    for (size_t i = 0; i < tcounts.size(); ++i) {
      counts[i].fetch_add(tcounts[i], std::memory_order_relaxed);
    }
  };

  // start test threads
  std::vector<std::thread> threads;
  for (auto it = ++cpus.begin(); it != cpus.end(); ++it) {
    threads.emplace_back([&, cpu = *it] { func(cpu); });
  }
  func(cpus.front());

  // wait for all threads to finish
  for (auto &t : threads) {
    t.join();
  }

  size_t endmask = 0xf;
  if (type == ALIGNED512 || type == SPLIT512) {
    endmask = 0xff;
  }

  int res = 0;
  for (size_t i = 0; i < counts.size(); i++) {
    if (counts[i] != 0) {
      if (i != 0 && i != endmask) {
        res = 1;
      }
      std::cout << std::setfill('0') << std::setw(2) << std::hex << i << " "
                << std::dec << counts[i]
                << (i != 0 && i != endmask ? " torn load/store!" : "")
                << std::endl;
    }
  }

  return res;
}