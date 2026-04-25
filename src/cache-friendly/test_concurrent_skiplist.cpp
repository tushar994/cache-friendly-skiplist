#include "concurrent_skiplist_read_write_mutex.h"
#include <optional>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <numeric>
#include <algorithm>
#include <chrono>

// ─── helpers ─────────────────────────────────────────────────────────────────

static std::atomic<int> passed{0};
static std::atomic<int> failed{0};
static std::mutex print_mutex;  // prevent interleaved output from threads

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

// ─── concurrent insert tests ─────────────────────────────────────────────────

// N threads each insert a disjoint range of keys — no key contention
void test_concurrent_inserts_disjoint_small() {
    std::cout << "\n-- test_concurrent_inserts_disjoint --\n";
    const int NUM_THREADS = 8;
    const int KEYS_PER_THREAD = 10;
    auto* sl = new SimpleSkiplist<int, int>();

    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_THREADS; t++) {
        threads.emplace_back([&, t]() {
            int start = t * KEYS_PER_THREAD;
            for (int i = start; i < start + KEYS_PER_THREAD; i++) {
                sl->insert_or_modify(i, i * 10);
            }
            tlog("thread " + std::to_string(t) + " finished inserting keys "
                 + std::to_string(t * KEYS_PER_THREAD) + "-"
                 + std::to_string(t * KEYS_PER_THREAD + KEYS_PER_THREAD - 1));
        });
    }
    for (auto& th : threads) th.join();
    sl->print();

    // verify all keys are present
    int failures = 0;
    for (int i = 0; i < NUM_THREADS * KEYS_PER_THREAD; i++) {
        auto r = sl->get(i);
        if (!r.has_value() || r.value() != i * 10) {
            tlog("FAIL: get(" + std::to_string(i) + ") expected " + std::to_string(i*10)
                 + " got " + (r.has_value() ? std::to_string(r.value()) : "nullopt"));
            failures++;
        }
    }
    check(failures == 0, "all " + std::to_string(NUM_THREADS * KEYS_PER_THREAD)
          + " disjoint keys inserted correctly across " + std::to_string(NUM_THREADS) + " threads",
          std::to_string(failures) + " failures");

    delete sl;
}
// N threads each insert a disjoint range of keys — no key contention
void test_concurrent_inserts_disjoint() {
    std::cout << "\n-- test_concurrent_inserts_disjoint --\n";
    const int NUM_THREADS = 8;
    const int KEYS_PER_THREAD = 100;
    auto* sl = new SimpleSkiplist<int, int>();

    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_THREADS; t++) {
        threads.emplace_back([&, t]() {
            int start = t * KEYS_PER_THREAD;
            for (int i = start; i < start + KEYS_PER_THREAD; i++) {
                sl->insert_or_modify(i, i * 10);
            }
            tlog("thread " + std::to_string(t) + " finished inserting keys "
                 + std::to_string(t * KEYS_PER_THREAD) + "-"
                 + std::to_string(t * KEYS_PER_THREAD + KEYS_PER_THREAD - 1));
        });
    }
    for (auto& th : threads) th.join();

    // verify all keys are present
    int failures = 0;
    for (int i = 0; i < NUM_THREADS * KEYS_PER_THREAD; i++) {
        auto r = sl->get(i);
        if (!r.has_value() || r.value() != i * 10) {
            tlog("FAIL: get(" + std::to_string(i) + ") expected " + std::to_string(i*10)
                 + " got " + (r.has_value() ? std::to_string(r.value()) : "nullopt"));
            failures++;
        }
    }
    check(failures == 0, "all " + std::to_string(NUM_THREADS * KEYS_PER_THREAD)
          + " disjoint keys inserted correctly across " + std::to_string(NUM_THREADS) + " threads",
          std::to_string(failures) + " failures");

    delete sl;
}

// N threads all insert the SAME keys — tests lock correctness under contention
void test_concurrent_inserts_same_keys() {
    std::cout << "\n-- test_concurrent_inserts_same_keys --\n";
    const int NUM_THREADS = 8;
    const int NUM_KEYS = 50;
    auto* sl = new SimpleSkiplist<int, int>();

    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_THREADS; t++) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < NUM_KEYS; i++) {
                sl->insert_or_modify(i, t * 1000 + i);  // each thread writes different values
            }
            tlog("thread " + std::to_string(t) + " finished");
        });
    }
    for (auto& th : threads) th.join();

    // all keys must exist — value will be whatever the last writer wrote, that's fine
    int failures = 0;
    for (int i = 0; i < NUM_KEYS; i++) {
        auto r = sl->get(i);
        if (!r.has_value()) {
            tlog("FAIL: key " + std::to_string(i) + " missing after concurrent inserts");
            failures++;
        }
    }
    check(failures == 0, "all " + std::to_string(NUM_KEYS)
          + " keys exist after " + std::to_string(NUM_THREADS) + " threads writing same keys",
          std::to_string(failures) + " missing keys");

    delete sl;
}

// ─── concurrent read tests ────────────────────────────────────────────────────

// insert data first, then N threads read concurrently
void test_concurrent_reads() {
    std::cout << "\n-- test_concurrent_reads --\n";
    const int NUM_THREADS = 8;
    const int NUM_KEYS = 200;
    auto* sl = new SimpleSkiplist<int, int>();

    // single-threaded setup
    for (int i = 0; i < NUM_KEYS; i++)
        sl->insert_or_modify(i, i * 5);
    tlog("inserted " + std::to_string(NUM_KEYS) + " keys before spawning readers");

    std::atomic<int> read_failures{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_THREADS; t++) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < NUM_KEYS; i++) {
                auto r = sl->get(i);
                if (!r.has_value() || r.value() != i * 5) {
                    read_failures++;
                }
            }
            tlog("thread " + std::to_string(t) + " finished reading");
        });
    }
    for (auto& th : threads) th.join();

    check(read_failures == 0, std::to_string(NUM_THREADS) + " threads reading concurrently — no failures",
          std::to_string(read_failures.load()) + " bad reads");

    delete sl;
}

// ─── mixed read/write tests ───────────────────────────────────────────────────

// half threads insert, half threads read simultaneously
void test_concurrent_reads_and_writes() {
    std::cout << "\n-- test_concurrent_reads_and_writes --\n";
    const int NUM_THREADS = 8;   // 4 writers, 4 readers
    const int NUM_KEYS = 100;
    auto* sl = new SimpleSkiplist<int, int>();

    // pre-insert all keys so readers always have something to find
    for (int i = 0; i < NUM_KEYS; i++)
        sl->insert_or_modify(i, i);
    tlog("pre-inserted " + std::to_string(NUM_KEYS) + " keys");

    std::atomic<int> read_failures{0};
    std::atomic<int> write_count{0};
    std::vector<std::thread> threads;

    // 4 writer threads — overwrite existing keys
    for (int t = 0; t < NUM_THREADS / 2; t++) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < NUM_KEYS; i++) {
                sl->insert_or_modify(i, i * 2);
                write_count++;
            }
            tlog("writer thread " + std::to_string(t) + " finished");
        });
    }

    // 4 reader threads — keys must always exist (pre-inserted)
    for (int t = 0; t < NUM_THREADS / 2; t++) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < NUM_KEYS; i++) {
                auto r = sl->get(i);
                if (!r.has_value()) {
                    read_failures++;
                    tlog("FAIL: reader thread " + std::to_string(t)
                         + " could not find key " + std::to_string(i));
                }
            }
            tlog("reader thread " + std::to_string(t) + " finished");
        });
    }

    for (auto& th : threads) th.join();
    tlog("total writes completed: " + std::to_string(write_count.load()));

    check(read_failures == 0, "readers never see missing keys during concurrent writes",
          std::to_string(read_failures.load()) + " missing key reads");

    delete sl;
}

// ─── concurrent remove tests ──────────────────────────────────────────────────

// threads remove disjoint key ranges
void test_concurrent_removes_disjoint() {
    std::cout << "\n-- test_concurrent_removes_disjoint --\n";
    const int NUM_THREADS = 4;
    const int KEYS_PER_THREAD = 50;
    const int TOTAL = NUM_THREADS * KEYS_PER_THREAD;
    auto* sl = new SimpleSkiplist<int, int>();

    for (int i = 0; i < TOTAL; i++)
        sl->insert_or_modify(i, i * 10);
    tlog("inserted " + std::to_string(TOTAL) + " keys");

    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_THREADS; t++) {
        threads.emplace_back([&, t]() {
            int start = t * KEYS_PER_THREAD;
            for (int i = start; i < start + KEYS_PER_THREAD; i++) {
                sl->remove(i);
            }
            tlog("thread " + std::to_string(t) + " removed keys "
                 + std::to_string(t * KEYS_PER_THREAD) + "-"
                 + std::to_string(t * KEYS_PER_THREAD + KEYS_PER_THREAD - 1));
        });
    }
    for (auto& th : threads) th.join();

    int failures = 0;
    for (int i = 0; i < TOTAL; i++) {
        auto r = sl->get(i);
        if (r.has_value()) {
            tlog("FAIL: key " + std::to_string(i) + " still exists after remove");
            failures++;
        }
    }
    check(failures == 0, "all keys removed correctly across " + std::to_string(NUM_THREADS) + " threads",
          std::to_string(failures) + " keys still present");

    delete sl;
}

// ─── mixed insert/remove/get ──────────────────────────────────────────────────

// all operations happening simultaneously — chaos test, just check it doesn't crash
void test_concurrent_chaos() {
    std::cout << "\n-- test_concurrent_chaos --\n";
    std::cout << "       (chaos test — just checking no crash/deadlock)\n";
    const int NUM_THREADS = 100;
    const int OPS_PER_THREAD = 200;
    const int KEY_RANGE = 50;  // small range so threads frequently hit same keys
    auto* sl = new SimpleSkiplist<int, int>();

    std::atomic<bool> crashed{false};
    std::vector<std::thread> threads;

    for (int t = 0; t < NUM_THREADS; t++) {
        threads.emplace_back([&, t]() {
            for (int op = 0; op < OPS_PER_THREAD; op++) {
                int key = (t * OPS_PER_THREAD + op) % KEY_RANGE;
                int type = op % 3;
                if (type == 0) {
                    sl->insert_or_modify(key, key * t);
                } else if (type == 1) {
                    sl->get(key);
                } else {
                    sl->remove(key);
                }
            }
            tlog("chaos thread " + std::to_string(t) + " finished");
        });
    }
    for (auto& th : threads) th.join();

    check(!crashed, "chaos test — " + std::to_string(NUM_THREADS)
          + " threads doing mixed insert/get/remove did not crash");

    delete sl;
}

// ─── concurrent range queries ─────────────────────────────────────────────────

void test_concurrent_range_reads() {
    std::cout << "\n-- test_concurrent_range_reads --\n";
    const int NUM_THREADS = 6;
    const int NUM_KEYS = 300;
    auto* sl = new SimpleSkiplist<int, int>();

    for (int i = 0; i < NUM_KEYS; i++)
        sl->insert_or_modify(i, i * 2);
    tlog("inserted " + std::to_string(NUM_KEYS) + " keys");

    std::atomic<int> failures{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < NUM_THREADS; t++) {
        threads.emplace_back([&, t]() {
            // each thread does a range starting at a different offset
            int start = t * 20;
            std::vector<std::pair<int,int>> results;
            sl->range(start, [&results](int k, int v) {
                results.push_back({k, v});
            }, 20);

            // verify order and values
            for (int i = 0; i < (int)results.size(); i++) {
                int expected_key = start + i;
                int expected_val = expected_key * 2;
                if (results[i].first != expected_key || results[i].second != expected_val) {
                    failures++;
                    tlog("FAIL: thread " + std::to_string(t)
                         + " result[" + std::to_string(i) + "] = ("
                         + std::to_string(results[i].first) + ","
                         + std::to_string(results[i].second) + ") expected ("
                         + std::to_string(expected_key) + ","
                         + std::to_string(expected_val) + ")");
                }
            }
            tlog("thread " + std::to_string(t) + " range(" + std::to_string(start)
                 + ", 20) got " + std::to_string(results.size()) + " results");
        });
    }
    for (auto& th : threads) th.join();

    check(failures == 0, std::to_string(NUM_THREADS) + " concurrent range queries all correct",
          std::to_string(failures.load()) + " bad results");

    delete sl;
}

// specifically tests that range + concurrent writers don't deadlock.
// if range leaks a lock, a writer will block forever and this test will hang.
void test_range_reads_during_writes() {
    std::cout << "\n-- test_range_reads_during_writes --\n";
    std::cout << "       (if this hangs, range is leaking a lock)\n";
    const int NUM_KEYS = 200;
    auto* sl = new SimpleSkiplist<int, int>();

    for (int i = 0; i < NUM_KEYS; i++)
        sl->insert_or_modify(i, i);
    tlog("pre-inserted " + std::to_string(NUM_KEYS) + " keys");

    std::atomic<bool> stop{false};
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

    // readers do range queries concurrently with writers
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
            tlog("range reader " + std::to_string(t) + " done after " + std::to_string(iter) + " range queries");
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    stop = true;
    for (auto& th : threads) th.join();

    check(true, "range reads during concurrent writes completed without deadlock");
    delete sl;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== Concurrent SimpleSkiplist Tests ===\n";

    test_concurrent_inserts_disjoint_small();
    test_concurrent_inserts_disjoint();
    test_concurrent_inserts_same_keys();
    test_concurrent_reads();
    test_concurrent_reads_and_writes();
    test_concurrent_removes_disjoint();
    test_concurrent_chaos();
    test_concurrent_range_reads();
    test_range_reads_during_writes();

    std::cout << "\n=== Results: " << passed << " passed, " << failed << " failed ===\n";
    return failed == 0 ? 0 : 1;
}
