#include "cache_friendly_skiplist.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

// ─── config ──────────────────────────────────────────────────────────────────

static constexpr int    NUM_KEYS      = 500'000;
static constexpr int    OPS_PER_BENCH = 1'000'000;
static constexpr int    RANGE_LEN     = 10;
static constexpr int    BENCH_HEIGHT  = 20;  // must cover log2(NUM_KEYS)

// thread counts to sweep
static const int THREAD_COUNTS[] = {1, 2, 4, 8};

// ─── helpers ─────────────────────────────────────────────────────────────────

// Generate a shuffled key universe so threads get roughly uniform distribution
static std::vector<uint64_t> make_keys(int n, uint64_t seed = 42) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<uint64_t> dist(1, (uint64_t)n * 10);
    std::vector<uint64_t> keys(n);
    for (auto& k : keys) k = dist(rng);
    return keys;
}

// Parallel for: split [0,n) across num_threads, block until all done.
// Returns wall-clock microseconds elapsed.
template <typename F>
static uint64_t parallel_timed(int num_threads, uint64_t n, F f) {
    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    uint64_t per = n / num_threads;

    auto t0 = std::chrono::high_resolution_clock::now();
    for (int t = 0; t < num_threads; t++) {
        uint64_t lo = t * per;
        uint64_t hi = (t == num_threads - 1) ? n : lo + per;
        threads.emplace_back([=, &f]() {
            for (uint64_t i = lo; i < hi; i++) f(i);
        });
    }
    for (auto& th : threads) th.join();
    auto t1 = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
}

// ─── bench harness ───────────────────────────────────────────────────────────

struct BenchResult {
    double load_mops;   // million ops/sec for the load phase
    double run_mops;    // million ops/sec for the run phase
};

// workload_fn receives the skiplist and a global op index i ∈ [0, OPS_PER_BENCH)
template <typename Workload>
static BenchResult run_bench(int num_threads,
                              const std::vector<uint64_t>& load_keys,
                              const std::vector<uint64_t>& run_keys,
                              Workload workload_fn) {
    SimpleSkiplist<uint64_t, uint64_t, BENCH_HEIGHT> sl;

    // load phase: bulk-insert load_keys in parallel
    uint64_t load_us = parallel_timed(num_threads, load_keys.size(),
        [&](uint64_t i) { sl.insert_or_modify(load_keys[i], load_keys[i]); });

    // run phase
    uint64_t run_us = parallel_timed(num_threads, OPS_PER_BENCH,
        [&](uint64_t i) { workload_fn(sl, i, run_keys); });

    return {
        (double)load_keys.size() / load_us,   // ops/us = M ops/s
        (double)OPS_PER_BENCH    / run_us
    };
}

// ─── workloads ───────────────────────────────────────────────────────────────

// 100% inserts
static void wl_write(SimpleSkiplist<uint64_t,uint64_t,BENCH_HEIGHT>& sl, uint64_t i,
                     const std::vector<uint64_t>& keys) {
    sl.insert_or_modify(keys[i % keys.size()], i);
}

// 100% reads
static void wl_read(SimpleSkiplist<uint64_t,uint64_t,BENCH_HEIGHT>& sl, uint64_t i,
                    const std::vector<uint64_t>& keys) {
    sl.get(keys[i % keys.size()]);
}

// 50% reads, 50% inserts
static void wl_mixed(SimpleSkiplist<uint64_t,uint64_t,BENCH_HEIGHT>& sl, uint64_t i,
                     const std::vector<uint64_t>& keys) {
    if (i % 2 == 0)
        sl.get(keys[i % keys.size()]);
    else
        sl.insert_or_modify(keys[i % keys.size()], i);
}

// 80% reads, 20% inserts
static void wl_read_heavy(SimpleSkiplist<uint64_t,uint64_t,BENCH_HEIGHT>& sl, uint64_t i,
                          const std::vector<uint64_t>& keys) {
    if (i % 5 != 0)
        sl.get(keys[i % keys.size()]);
    else
        sl.insert_or_modify(keys[i % keys.size()], i);
}

// range scans
static void wl_scan(SimpleSkiplist<uint64_t,uint64_t,BENCH_HEIGHT>& sl, uint64_t i,
                    const std::vector<uint64_t>& keys) {
    volatile uint64_t sink = 0;
    sl.range(keys[i % keys.size()], [&](uint64_t k, uint64_t v) {
        sink += k + v;
    }, RANGE_LEN);
}

// ─── main ────────────────────────────────────────────────────────────────────

static void print_header() {
    std::cout << "\n"
              << std::setw(16) << "workload"
              << std::setw(10) << "threads"
              << std::setw(16) << "load (Mops/s)"
              << std::setw(16) << "run  (Mops/s)"
              << "\n"
              << std::string(58, '-') << "\n";
}

static void print_row(const std::string& name, int threads, BenchResult r) {
    std::cout << std::setw(16) << name
              << std::setw(10) << threads
              << std::setw(16) << std::fixed << std::setprecision(3) << r.load_mops
              << std::setw(16) << std::fixed << std::setprecision(3) << r.run_mops
              << "\n";
}

int main() {
    std::cout << "cache-friendly B-skiplist benchmark\n";
    std::cout << "  NUM_KEYS=" << NUM_KEYS
              << "  OPS_PER_BENCH=" << OPS_PER_BENCH
              << "  ARRAY_SIZE=" << ARRAY_SIZE << "\n";

    auto load_keys = make_keys(NUM_KEYS, 1);
    auto run_keys  = make_keys(OPS_PER_BENCH, 2);

    struct WL { const char* name; void(*fn)(SimpleSkiplist<uint64_t,uint64_t,BENCH_HEIGHT>&, uint64_t, const std::vector<uint64_t>&); };
    WL workloads[] = {
        { "write-only",  wl_write      },
        { "read-only",   wl_read       },
        { "mixed-50/50", wl_mixed      },
        { "read-heavy",  wl_read_heavy },
        { "scan",        wl_scan       },
    };

    print_header();
    for (auto& wl : workloads) {
        std::cout << "\n";
        for (int t : THREAD_COUNTS) {
            BenchResult r = run_bench(t, load_keys, run_keys, wl.fn);
            print_row(wl.name, t, r);
        }
    }
    std::cout << "\n";
    return 0;
}
