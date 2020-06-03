// © 2020 Erik Rigtorp <erik@rigtorp.se>
// SPDX-License-Identifier: MIT

#include <atomic>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include <immintrin.h>

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
    std::cerr
        << "isatomic 1.0.0 © 2020 Erik Rigtorp <erik@rigtorp.se>\n"
           "usage: isatomic [-i iters] -t 128|128u|128s|256|256u|256s\n"
           "tests if 16B/32B wide loads/stores are atomic\n"
           "number of iterations defaults to 1000000\n"
           "128:  16B loads/stores\n"
           "128u: 16B unaligned loads/stores\n"
           "128s: 16B cacheline split loads/stores\n"
           "256:  32B loads/stores\n"
           "256u: 32B unaligned loads/stores\n"
           "256s: 32B cacheline split loads/stores\n"
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

  alignas(64) __m128i foo128 = _mm_set1_epi64x(0);
  alignas(64) __m256i foo256 = _mm256_set1_epi64x(0);
  alignas(64) char buf[128] = {};

  std::array<std::atomic<size_t>, 16> counts = {};
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

    std::array<size_t, 16> tcounts = {};

    // wait for all threads to be ready
    active_threads.fetch_add(1, std::memory_order_relaxed);
    while (active_threads.load(std::memory_order_relaxed) != cpus.size())
      ;

    switch (type) {
    case ALIGNED128:
      for (size_t i = 0; i < iters; ++i) {
        int x = _mm_movemask_ps((__m128)_mm_load_si128(&foo128));
        tcounts[x]++;
        _mm_store_si128(&foo128, _mm_set1_epi32(i % 2 ? 0 : -1));
      }
      break;
    case UNALIGNED128:
      for (size_t i = 0; i < iters; ++i) {
        int x = _mm_movemask_ps((__m128)_mm_loadu_si128((__m128i *)&buf[3]));
        tcounts[x]++;
        _mm_storeu_si128((__m128i *)&buf[3], _mm_set1_epi32(i % 2 ? 0 : -1));
      }
      break;
    case SPLIT128:
      for (size_t i = 0; i < iters; ++i) {
        int x = _mm_movemask_ps((__m128)_mm_loadu_si128((__m128i *)&buf[56]));
        tcounts[x]++;
        _mm_storeu_si128((__m128i *)&buf[56], _mm_set1_epi32(i % 2 ? 0 : -1));
      }
      break;
    case ALIGNED256:
      for (size_t i = 0; i < iters; ++i) {
        int x = _mm256_movemask_pd((__m256d)_mm256_load_si256(&foo256));
        tcounts[x]++;
        _mm256_store_si256(&foo256, _mm256_set1_epi64x(i % 2 ? 0 : -1));
      }
      break;
    case UNALIGNED256:
      for (size_t i = 0; i < iters; ++i) {
        int x =
            _mm256_movemask_pd((__m256d)_mm256_loadu_si256((__m256i *)&buf[3]));
        tcounts[x]++;
        _mm256_storeu_si256((__m256i *)&buf[3],
                            _mm256_set1_epi64x(i % 2 ? 0 : -1));
      }
      break;
    case SPLIT256:
      for (size_t i = 0; i < iters; ++i) {
        int x = _mm256_movemask_pd(
            (__m256d)_mm256_loadu_si256((__m256i *)&buf[48]));
        tcounts[x]++;
        _mm256_storeu_si256((__m256i *)&buf[48],
                            _mm256_set1_epi64x(i % 2 ? 0 : -1));
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

  int res = 0;
  for (size_t i = 0; i < counts.size(); i++) {
    if (counts[i] != 0) {
      if (i != 0 && i != 0xf) {
        res = 1;
      }
      std::cout << std::hex << i << " " << std::dec << counts[i]
                << (i != 0 && i != 0xf ? " torn load/store!" : "") << std::endl;
    }
  }

  return res;
}