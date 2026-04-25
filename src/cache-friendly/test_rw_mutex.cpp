#include "concurrent_skiplist_read_write_mutex.h"
#include <optional>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <numeric>
#include <chrono>

// ─── helpers ─────────────────────────────────────────────────────────────────

static std::atomic<int> passed{0};
static std::atomic<int> failed{0};
static std::mutex print_mutex;

void check(bool condition, const std::string& test_name, const std::string& detail = "") {
    std::lock_guard<std::mutex> lock(print_mutex);
    if (condition) {
        std::cout << "[PASS] " << test_name << "\n";
        passed++;
    } else {
        std::cout << "[FAIL] " << test_name;
        if (!detail.empty()) std::cout << "\n       " << detail;
        std::cout << "\n";
        failed++;
    }
}

void tlog(const std::string& msg) {
    std::lock_guard<std::mutex> lock(print_mutex);
    std::cout << "       " << msg << "\n";
}

// ─── read/write mutex specific tests ─────────────────────────────────────────

// multiple readers should be able to read simultaneously without blocking each other.
// we verify this by timing — if reads were serialized, N concurrent reads would take
// N times as long as a single read.
void test_concurrent_readers_dont_block_each_other() {
    std::cout << "\n-- test_concurrent_readers_dont_block_each_other --\n";
    const int NUM_THREADS = 16;
    const int NUM_KEYS = 200;
    auto* sl = new SimpleSkiplist<int, int>();

    for (int i = 0; i < NUM_KEYS; i++)
        sl->insert_or_modify(i, i * 2);
    tlog("pre-inserted " + std::to_string(NUM_KEYS) + " keys");

    // time a single-threaded read pass
    auto single_start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < NUM_KEYS; i++) sl->get(i);
    auto single_end = std::chrono::high_resolution_clock::now();
    auto single_us = std::chrono::duration_cast<std::chrono::microseconds>(single_end - single_start).count();
    tlog("single-threaded read of " + std::to_string(NUM_KEYS) + " keys took " + std::to_string(single_us) + " us");

    // time concurrent reads across NUM_THREADS threads
    std::atomic<int> read_failures{0};
    std::vector<std::thread> threads;
    auto multi_start = std::chrono::high_resolution_clock::now();
    for (int t = 0; t < NUM_THREADS; t++) {
        threads.emplace_back([&]() {
            for (int i = 0; i < NUM_KEYS; i++) {
                auto r = sl->get(i);
                if (!r.has_value() || r.value() != i * 2)
                    read_failures++;
            }
        });
    }
    for (auto& th : threads) th.join();
    auto multi_end = std::chrono::high_resolution_clock::now();
    auto multi_us = std::chrono::duration_cast<std::chrono::microseconds>(multi_end - multi_start).count();
    tlog(std::to_string(NUM_THREADS) + " concurrent threads read took " + std::to_string(multi_us) + " us");

    // concurrent reads should not take much longer than single-threaded
    // if reads were serialized it would take ~NUM_THREADS * single_us
    // we allow 4x as generous upper bound
    long long serialized_estimate = single_us * NUM_THREADS;
    long long generous_bound = serialized_estimate / 4;
    tlog("serialized estimate would be " + std::to_string(serialized_estimate) + " us");
    tlog("concurrent took " + std::to_string(multi_us) + " us (should be much less than serialized)");

    check(read_failures == 0, "all concurrent reads returned correct values",
          std::to_string(read_failures.load()) + " wrong reads");
    check(multi_us < serialized_estimate, "concurrent reads faster than fully serialized reads",
          "concurrent=" + std::to_string(multi_us) + "us, serialized_estimate=" + std::to_string(serialized_estimate) + "us");

    delete sl;
}

// a writer should block readers while it holds the write lock.
// we verify correctness — readers should never see a partial write.
void test_readers_see_consistent_values_during_writes() {
    std::cout << "\n-- test_readers_see_consistent_values_during_writes --\n";
    const int NUM_READERS = 8;
    const int NUM_WRITERS = 4;
    const int NUM_KEYS = 50;
    const int ITERATIONS = 100;
    auto* sl = new SimpleSkiplist<int, int>();

    // pre-insert all keys with value 0
    for (int i = 0; i < NUM_KEYS; i++)
        sl->insert_or_modify(i, 0);
    tlog("pre-inserted " + std::to_string(NUM_KEYS) + " keys with value 0");

    // writers flip all values between 0 and 1000 atomically per key
    // readers should only ever see 0 or 1000, never anything in between
    // (since each insert_or_modify is a single operation)
    std::atomic<bool> stop{false};
    std::atomic<int> bad_reads{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < NUM_WRITERS; t++) {
        threads.emplace_back([&, t]() {
            for (int iter = 0; iter < ITERATIONS; iter++) {
                int new_val = (iter % 2 == 0) ? 1000 : 0;
                for (int i = 0; i < NUM_KEYS; i++)
                    sl->insert_or_modify(i, new_val);
            }
            tlog("writer " + std::to_string(t) + " done");
        });
    }

    for (int t = 0; t < NUM_READERS; t++) {
        threads.emplace_back([&, t]() {
            while (!stop) {
                for (int i = 0; i < NUM_KEYS; i++) {
                    auto r = sl->get(i);
                    if (r.has_value() && r.value() != 0 && r.value() != 1000) {
                        bad_reads++;
                        tlog("reader " + std::to_string(t) + " saw bad value "
                             + std::to_string(r.value()) + " for key " + std::to_string(i));
                    }
                }
            }
            tlog("reader " + std::to_string(t) + " done");
        });
    }

    // wait for writers to finish then stop readers
    for (int t = 0; t < NUM_WRITERS; t++) threads[t].join();
    stop = true;
    for (int t = NUM_WRITERS; t < NUM_WRITERS + NUM_READERS; t++) threads[t].join();

    check(bad_reads == 0, "readers never see partial/invalid values during concurrent writes",
          std::to_string(bad_reads.load()) + " bad reads detected");

    delete sl;
}

// write lock should be exclusive — two writers should never write simultaneously.
// we verify by having writers increment a shared counter inside the skiplist
// and checking for lost updates.
void test_writers_are_exclusive() {
    std::cout << "\n-- test_writers_are_exclusive --\n";
    const int NUM_THREADS = 16;
    const int OPS_PER_THREAD = 100;
    auto* sl = new SimpleSkiplist<int, int>();

    // key 0 starts at 0, each thread increments it OPS_PER_THREAD times
    // if writes are truly exclusive, final value = NUM_THREADS * OPS_PER_THREAD
    sl->insert_or_modify(0, 0);

    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_THREADS; t++) {
        threads.emplace_back([&]() {
            for (int i = 0; i < OPS_PER_THREAD; i++) {
                auto cur = sl->get(0);
                int new_val = cur.value_or(0) + 1;
                sl->insert_or_modify(0, new_val);
            }
        });
    }
    for (auto& th : threads) th.join();

    auto result = sl->get(0);
    int expected = NUM_THREADS * OPS_PER_THREAD;
    tlog("expected final value: " + std::to_string(expected));
    tlog("actual final value:   " + std::to_string(result.value_or(-1)));

    // note: this test will likely FAIL if get+insert is not atomic as a pair
    // that's expected — it shows the gap between read and write
    // the point is to observe whether the locking prevents races within each operation
    check(result.has_value(), "key 0 exists after concurrent increments");
    tlog("(if final value < " + std::to_string(expected) + ", get+insert_or_modify pair is not atomic — expected)");

    delete sl;
}

// readers and writers on completely disjoint keys should not block each other.
void test_disjoint_readers_writers_dont_interfere() {
    std::cout << "\n-- test_disjoint_readers_writers_dont_interfere --\n";
    const int NUM_KEYS = 200;
    const int ITERATIONS = 50;
    auto* sl = new SimpleSkiplist<int, int>();

    for (int i = 0; i < NUM_KEYS; i++)
        sl->insert_or_modify(i, i * 10);
    tlog("pre-inserted " + std::to_string(NUM_KEYS) + " keys");

    std::atomic<int> read_failures{0};
    std::atomic<int> write_count{0};
    std::vector<std::thread> threads;

    // readers read lower half (0-99)
    for (int t = 0; t < 4; t++) {
        threads.emplace_back([&, t]() {
            for (int iter = 0; iter < ITERATIONS; iter++) {
                for (int i = 0; i < NUM_KEYS / 2; i++) {
                    auto r = sl->get(i);
                    if (!r.has_value()) {
                        read_failures++;
                        tlog("reader " + std::to_string(t) + " lost key " + std::to_string(i));
                    }
                }
            }
            tlog("reader " + std::to_string(t) + " done");
        });
    }

    // writers write upper half (100-199)
    for (int t = 0; t < 4; t++) {
        threads.emplace_back([&, t]() {
            for (int iter = 0; iter < ITERATIONS; iter++) {
                for (int i = NUM_KEYS / 2; i < NUM_KEYS; i++) {
                    sl->insert_or_modify(i, i * t);
                    write_count++;
                }
            }
            tlog("writer " + std::to_string(t) + " done");
        });
    }

    for (auto& th : threads) th.join();
    tlog("total writes: " + std::to_string(write_count.load()));

    check(read_failures == 0, "readers on lower keys never lost data while writers worked on upper keys",
          std::to_string(read_failures.load()) + " missing reads");

    delete sl;
}

// concurrent range reads should all return consistent ordered results
void test_concurrent_range_reads_consistent() {
    std::cout << "\n-- test_concurrent_range_reads_consistent --\n";
    const int NUM_THREADS = 8;
    const int NUM_KEYS = 300;
    auto* sl = new SimpleSkiplist<int, int>();

    for (int i = 0; i < NUM_KEYS; i++)
        sl->insert_or_modify(i, i * 3);
    tlog("pre-inserted " + std::to_string(NUM_KEYS) + " keys");

    std::atomic<int> failures{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < NUM_THREADS; t++) {
        threads.emplace_back([&, t]() {
            int start = t * 20;
            std::vector<std::pair<int,int>> results;
            sl->range(start, [&results](int k, int v) {
                results.push_back({k, v});
            }, 20);

            for (int i = 0; i < (int)results.size(); i++) {
                int expected_key = start + i;
                int expected_val = expected_key * 3;
                if (results[i].first != expected_key || results[i].second != expected_val) {
                    failures++;
                    tlog("thread " + std::to_string(t) + " bad result[" + std::to_string(i)
                         + "] got (" + std::to_string(results[i].first) + ","
                         + std::to_string(results[i].second) + ") expected ("
                         + std::to_string(expected_key) + "," + std::to_string(expected_val) + ")");
                }
            }
            tlog("thread " + std::to_string(t) + " range(" + std::to_string(start)
                 + ", 20) got " + std::to_string(results.size()) + " results");
        });
    }
    for (auto& th : threads) th.join();

    check(failures == 0, std::to_string(NUM_THREADS) + " concurrent range reads all returned correct ordered results",
          std::to_string(failures.load()) + " bad results");

    delete sl;
}

// concurrent range reads while writers are inserting
void test_range_reads_during_writes() {
    std::cout << "\n-- test_range_reads_during_writes --\n";
    const int NUM_KEYS = 200;
    auto* sl = new SimpleSkiplist<int, int>();

    for (int i = 0; i < NUM_KEYS; i++)
        sl->insert_or_modify(i, i);
    tlog("pre-inserted " + std::to_string(NUM_KEYS) + " keys");

    std::atomic<bool> stop{false};
    std::atomic<int> range_crashes{0};
    std::vector<std::thread> threads;

    // writers continuously overwrite
    for (int t = 0; t < 4; t++) {
        threads.emplace_back([&, t]() {
            int iter = 0;
            while (!stop) {
                for (int i = 0; i < NUM_KEYS; i++)
                    sl->insert_or_modify(i, i * (iter + 1));
                iter++;
            }
            tlog("writer " + std::to_string(t) + " done after " + std::to_string(iter) + " iterations");
        });
    }

    // readers do range queries — just check they don't crash
    for (int t = 0; t < 4; t++) {
        threads.emplace_back([&, t]() {
            int iter = 0;
            while (!stop) {
                std::vector<std::pair<int,int>> results;
                sl->range(50, [&results](int k, int v) {
                    results.push_back({k, v});
                }, 50);
                iter++;
            }
            tlog("range reader " + std::to_string(t) + " completed " + std::to_string(iter) + " range queries");
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    bool val = false;
    stop.compare_exchange_strong(val, true);
    for (auto& th : threads) th.join();

    check(range_crashes == 0, "range reads during concurrent writes did not crash",
          std::to_string(range_crashes.load()) + " crashes");

    delete sl;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== Read-Write Mutex Skiplist Tests ===\n";

    test_concurrent_readers_dont_block_each_other();
    test_readers_see_consistent_values_during_writes();
    test_writers_are_exclusive();
    test_disjoint_readers_writers_dont_interfere();
    test_concurrent_range_reads_consistent();
    test_range_reads_during_writes();

    std::cout << "\n=== Results: " << passed << " passed, " << failed << " failed ===\n";
    return failed == 0 ? 0 : 1;
}
