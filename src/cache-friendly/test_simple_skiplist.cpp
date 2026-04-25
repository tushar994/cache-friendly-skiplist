#include "cache_friendly_skiplist.h"
#include <optional>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <numeric>

// ─── helpers ────────────────────────────────────────────────────────────────

static int passed = 0;
static int failed = 0;

void check(bool condition, const std::string& test_name, const std::string& detail = "") {
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

template <typename V>
void print_result(const std::string& label, const std::optional<V>& result) {
    if (result.has_value())
        std::cout << "       " << label << " = " << result.value() << "\n";
    else
        std::cout << "       " << label << " = nullopt (not found)\n";
}

// ─── test groups ─────────────────────────────────────────────────────────────

void test_get_on_empty() {
    std::cout << "\n-- test_get_on_empty --\n";
    auto* sl = new SimpleSkiplist<int, int>();

    auto result = sl->get(42);
    print_result("get(42)", result);
    check(!result.has_value(), "get on empty skiplist returns nullopt");

    std::cout << "       deleting skiplist\n";
    delete sl;
    std::cout << "       deleted ok\n";
}

void test_insert_then_get() {
    std::cout << "\n-- test_insert_then_get --\n";
    auto* sl = new SimpleSkiplist<int, int>();

    sl->insert_or_modify(10, 100);
    std::cout << "       inserted (10 -> 100)\n";

    sl->print();

    auto result = sl->get(10);
    print_result("get(10)", result);
    check(result.has_value(),         "get(10) has value after insert");
    check(result.value_or(-1) == 100, "get(10) returns correct value 100");

    std::cout << "       deleting skiplist\n";
    delete sl;
    std::cout << "       deleted ok\n";
}

void test_get_missing_key() {
    std::cout << "\n-- test_get_missing_key --\n";
    auto* sl = new SimpleSkiplist<int, int>();

    sl->insert_or_modify(10, 100);
    std::cout << "       inserted (10 -> 100)\n";

    auto result = sl->get(99);
    print_result("get(99)", result);
    check(!result.has_value(), "get(99) returns nullopt for missing key");

    std::cout << "       deleting skiplist\n";
    delete sl;
    std::cout << "       deleted ok\n";
}

void test_modify_existing_key() {
    std::cout << "\n-- test_modify_existing_key --\n";
    auto* sl = new SimpleSkiplist<int, int>();

    sl->insert_or_modify(10, 100);
    std::cout << "       inserted (10 -> 100)\n";

    sl->insert_or_modify(10, 999);
    std::cout << "       modified (10 -> 999)\n";

    auto result = sl->get(10);
    print_result("get(10)", result);
    check(result.has_value(),         "get(10) has value after modify");
    check(result.value_or(-1) == 999, "get(10) returns updated value 999, not old 100");

    std::cout << "       deleting skiplist\n";
    delete sl;
    std::cout << "       deleted ok\n";
}

void test_multiple_inserts_in_order() {
    std::cout << "\n-- test_multiple_inserts_in_order --\n";
    auto* sl = new SimpleSkiplist<int, int>();

    sl->insert_or_modify(1, 10);
    sl->insert_or_modify(2, 20);
    sl->insert_or_modify(3, 30);
    std::cout << "       inserted (1->10), (2->20), (3->30)\n";

    auto r1 = sl->get(1); print_result("get(1)", r1);
    auto r2 = sl->get(2); print_result("get(2)", r2);
    auto r3 = sl->get(3); print_result("get(3)", r3);

    check(r1.value_or(-1) == 10, "get(1) == 10");
    check(r2.value_or(-1) == 20, "get(2) == 20");
    check(r3.value_or(-1) == 30, "get(3) == 30");

    std::cout << "       deleting skiplist\n";
    delete sl;
    std::cout << "       deleted ok\n";
}

void test_multiple_inserts_out_of_order() {
    std::cout << "\n-- test_multiple_inserts_out_of_order --\n";
    auto* sl = new SimpleSkiplist<int, int>();

    sl->insert_or_modify(30, 300);
    sl->insert_or_modify(10, 100);
    sl->insert_or_modify(20, 200);
    std::cout << "       inserted (30->300), (10->100), (20->200) — out of order\n";

    auto r1 = sl->get(10); print_result("get(10)", r1);
    auto r2 = sl->get(20); print_result("get(20)", r2);
    auto r3 = sl->get(30); print_result("get(30)", r3);

    check(r1.value_or(-1) == 100, "get(10) == 100");
    check(r2.value_or(-1) == 200, "get(20) == 200");
    check(r3.value_or(-1) == 300, "get(30) == 300");

    std::cout << "       deleting skiplist\n";
    delete sl;
    std::cout << "       deleted ok\n";
}

void test_insert_at_boundaries() {
    std::cout << "\n-- test_insert_at_boundaries --\n";
    auto* sl = new SimpleSkiplist<int, int>();

    sl->insert_or_modify(0,    0);
    sl->insert_or_modify(-100, -1000);
    sl->insert_or_modify(9999, 99990);
    std::cout << "       inserted (0->0), (-100->-1000), (9999->99990)\n";

    auto r1 = sl->get(0);    print_result("get(0)",    r1);
    auto r2 = sl->get(-100); print_result("get(-100)", r2);
    auto r3 = sl->get(9999); print_result("get(9999)", r3);

    check(r1.value_or(-999) == 0,      "get(0) == 0");
    check(r2.value_or(-999) == -1000,  "get(-100) == -1000");
    check(r3.value_or(-999) == 99990,  "get(9999) == 99990");

    std::cout << "       deleting skiplist\n";
    delete sl;
    std::cout << "       deleted ok\n";
}

void test_destructor_on_empty() {
    std::cout << "\n-- test_destructor_on_empty --\n";
    auto* sl = new SimpleSkiplist<int, int>();
    std::cout << "       created empty skiplist, deleting immediately\n";
    delete sl;
    std::cout << "       deleted ok\n";
    check(true, "destructor on empty skiplist does not crash");
}

void test_destructor_on_full() {
    std::cout << "\n-- test_destructor_on_full --\n";
    auto* sl = new SimpleSkiplist<int, int>();
    for (int i = 0; i < 20; i++) {
        sl->insert_or_modify(i, i * 10);
    }
    std::cout << "       inserted 20 elements, deleting\n";
    delete sl;
    std::cout << "       deleted ok\n";
    check(true, "destructor on populated skiplist does not crash");
}

// ─── delete tests (uncomment once you add remove()) ─────────────────────────

void test_delete_existing_key() {
    std::cout << "\n-- test_delete_existing_key --\n";
    auto* sl = new SimpleSkiplist<int, int>();

    sl->insert_or_modify(10, 100);
    std::cout << "       inserted (10 -> 100)\n";
    sl->print();
    sl->remove(10);
    std::cout << "       removed key 10\n";
    sl->print();

    auto result = sl->get(10);
    print_result("get(10) after delete", result);
    check(!result.has_value(), "get(10) returns nullopt after delete");

    std::cout << "       deleting skiplist\n";
    delete sl;
    std::cout << "       deleted ok\n";
}

void test_delete_then_reinsert() {
    std::cout << "\n-- test_delete_then_reinsert --\n";
    auto* sl = new SimpleSkiplist<int, int>();

    sl->insert_or_modify(10, 100);
    std::cout << "       inserted (10 -> 100)\n";
    sl->remove(10);
    std::cout << "       removed key 10\n";
    sl->insert_or_modify(10, 555);
    std::cout << "       re-inserted (10 -> 555)\n";

    auto result = sl->get(10);
    print_result("get(10) after re-insert", result);
    check(result.value_or(-1) == 555, "get(10) == 555 after delete and re-insert");

    std::cout << "       deleting skiplist\n";
    delete sl;
    std::cout << "       deleted ok\n";
}

void test_replace() {
    std::cout << "\n-- test_delete_then_reinsert --\n";
    auto* sl = new SimpleSkiplist<int, int>();

    sl->insert_or_modify(10, 100);
    std::cout << "       inserted (10 -> 100)\n";
    // sl->print();
    sl->insert_or_modify(10, 555);
    std::cout << "       re-inserted (10 -> 555)\n";
    // sl->print();

    auto result = sl->get(10);
    print_result("get(10) after re-insert", result);
    check(result.value_or(-1) == 555, "get(10) == 555 after delete and re-insert");

    std::cout << "       deleting skiplist\n";
    delete sl;
    std::cout << "       deleted ok\n";
}

void test_delete_nonexistent_key() {
    std::cout << "\n-- test_delete_nonexistent_key --\n";
    auto* sl = new SimpleSkiplist<int, int>();

    sl->insert_or_modify(10, 100);
    std::cout << "       inserted (10 -> 100)\n";
    std::cout << "       removing key 99 (does not exist)\n";
    sl->remove(99);  // should not crash

    auto result = sl->get(10);
    print_result("get(10) after deleting unrelated key", result);
    check(result.value_or(-1) == 100, "get(10) still == 100 after deleting unrelated key");

    std::cout << "       deleting skiplist\n";
    delete sl;
    std::cout << "       deleted ok\n";
}

void test_delete_one_of_many() {
    std::cout << "\n-- test_delete_one_of_many --\n";
    auto* sl = new SimpleSkiplist<int, int>();

    for(int i=0;i<10;i++){
        sl->insert_or_modify(i, i*10);
    }
    std::cout << "       inserted a bunch\n";
    sl->print();
    
    for(int i=0;i<10;i++){
        sl->insert_or_modify(i, 2*i*10);
    }
    std::cout << "       reinserted a bunch\n";
    sl->print();
    
    sl->remove(2);
    std::cout << "       removed key 2\n";
    sl->print();

    auto r1 = sl->get(1); print_result("get(1)", r1);
    auto r2 = sl->get(2); print_result("get(2)", r2);
    auto r3 = sl->get(3); print_result("get(3)", r3);

    check(r1.value_or(-1) == 10,  "get(1) still == 10");
    check(!r2.has_value(),         "get(2) == nullopt after delete");
    check(r3.value_or(-1) == 30,  "get(3) still == 30");

    std::cout << "       deleting skiplist\n";
    delete sl;
    std::cout << "       deleted ok\n";
}

void test_stress() {
    std::cout << "\n-- test_stress --\n";
    const int N = 1000;
    auto* sl = new SimpleSkiplist<int, int>();

    // --- insert 0..N-1 in sequential order ---
    std::cout << "       inserting " << N << " elements sequentially\n";
    for (int i = 0; i < N; i++)
        sl->insert_or_modify(i, i * 10);

    // verify all inserted values are retrievable
    int sequential_failures = 0;
    for (int i = 0; i < N; i++) {
        auto r = sl->get(i);
        if (!r.has_value() || r.value() != i * 10) {
            std::cout << "       [FAIL] get(" << i << ") expected " << i*10
                      << " got " << (r.has_value() ? std::to_string(r.value()) : "nullopt") << "\n";
            sequential_failures++;
        }
    }
    check(sequential_failures == 0, "all " + std::to_string(N) + " sequential inserts retrievable",
          std::to_string(sequential_failures) + " failures");

    // --- overwrite all values ---
    std::cout << "       overwriting all " << N << " elements\n";
    for (int i = 0; i < N; i++)
        sl->insert_or_modify(i, i * 99);

    int overwrite_failures = 0;
    for (int i = 0; i < N; i++) {
        auto r = sl->get(i);
        if (!r.has_value() || r.value() != i * 99) {
            std::cout << "       [FAIL] after overwrite get(" << i << ") expected " << i*99
                      << " got " << (r.has_value() ? std::to_string(r.value()) : "nullopt") << "\n";
            overwrite_failures++;
        }
    }
    check(overwrite_failures == 0, "all " + std::to_string(N) + " overwrites correctly updated",
          std::to_string(overwrite_failures) + " failures");

    // --- verify keys just outside range are not found ---
    std::cout << "       checking boundary keys not found\n";
    check(!sl->get(-1).has_value(),  "get(-1) not found");
    check(!sl->get(N).has_value(),   "get(" + std::to_string(N) + ") not found");
    check(!sl->get(9999).has_value(),"get(9999) not found");

    // --- delete every other key ---
    std::cout << "       deleting every even key (0, 2, 4, ...)\n";
    for (int i = 0; i < N; i += 2)
        sl->remove(i);

    int delete_failures = 0;
    for (int i = 0; i < N; i++) {
        auto r = sl->get(i);
        bool should_exist = (i % 2 != 0);
        bool exists = r.has_value();
        if (exists != should_exist) {
            std::cout << "       [FAIL] key " << i << " should "
                      << (should_exist ? "exist" : "not exist")
                      << " but " << (exists ? "was found" : "was not found") << "\n";
            delete_failures++;
        }
    }
    check(delete_failures == 0, "even keys deleted, odd keys still present",
          std::to_string(delete_failures) + " failures");

    // --- insert shuffled keys ---
    std::cout << "       inserting 500 shuffled keys (2000-2499)\n";
    std::vector<int> keys(500);
    std::iota(keys.begin(), keys.end(), 2000);  // 2000..2499
    std::reverse(keys.begin(), keys.end());     // insert in reverse order
    for (int k : keys)
        sl->insert_or_modify(k, k * 3);

    int shuffled_failures = 0;
    for (int k : keys) {
        auto r = sl->get(k);
        if (!r.has_value() || r.value() != k * 3) {
            std::cout << "       [FAIL] shuffled get(" << k << ") expected " << k*3
                      << " got " << (r.has_value() ? std::to_string(r.value()) : "nullopt") << "\n";
            shuffled_failures++;
        }
    }
    check(shuffled_failures == 0, "500 reverse-order inserts all retrievable",
          std::to_string(shuffled_failures) + " failures");

    std::cout << "       deleting skiplist\n";
    delete sl;
    std::cout << "       deleted ok\n";
    check(true, "destructor on stress-test skiplist does not crash");
}

// ─── range tests ─────────────────────────────────────────────────────────────

void test_range_basic() {
    std::cout << "\n-- test_range_basic --\n";
    auto* sl = new SimpleSkiplist<int, int>();

    sl->insert_or_modify(1, 10);
    sl->insert_or_modify(2, 20);
    sl->insert_or_modify(3, 30);
    sl->insert_or_modify(4, 40);
    sl->insert_or_modify(5, 50);
    std::cout << "       inserted (1->10),(2->20),(3->30),(4->40),(5->50)\n";

    std::vector<std::pair<int,int>> results;
    sl->range(2, [&results](int k, int v) {
        results.push_back({k, v});
    }, 3);

    std::cout << "       range(start=2, length=3) got " << results.size() << " results:\n";
    for (auto& [k, v] : results)
        std::cout << "           " << k << " -> " << v << "\n";

    check(results.size() == 3,          "range returns 3 results");
    check(results[0] == std::make_pair(2, 20), "first result is (2, 20)");
    check(results[1] == std::make_pair(3, 30), "second result is (3, 30)");
    check(results[2] == std::make_pair(4, 40), "third result is (4, 40)");

    delete sl;
}

void test_range_from_beginning() {
    std::cout << "\n-- test_range_from_beginning --\n";
    auto* sl = new SimpleSkiplist<int, int>();

    for (int i = 1; i <= 5; i++)
        sl->insert_or_modify(i, i * 10);
    std::cout << "       inserted 1-5\n";

    std::vector<std::pair<int,int>> results;
    sl->range(1, [&results](int k, int v) {
        results.push_back({k, v});
    }, 5);

    std::cout << "       range(start=1, length=5) got " << results.size() << " results\n";
    for (auto& [k, v] : results)
        std::cout << "           " << k << " -> " << v << "\n";

    check(results.size() == 5, "range from beginning returns all 5 elements");
    check(results[0] == std::make_pair(1, 10), "first element is (1, 10)");
    check(results[4] == std::make_pair(5, 50), "last element is (5, 50)");

    delete sl;
}

void test_range_length_exceeds_remaining() {
    std::cout << "\n-- test_range_length_exceeds_remaining --\n";
    auto* sl = new SimpleSkiplist<int, int>();

    sl->insert_or_modify(1, 10);
    sl->insert_or_modify(2, 20);
    sl->insert_or_modify(3, 30);
    std::cout << "       inserted (1->10),(2->20),(3->30)\n";

    std::vector<std::pair<int,int>> results;
    sl->range(2, [&results](int k, int v) {
        results.push_back({k, v});
    }, 100);  // ask for 100 but only 2 exist from key 2 onwards

    std::cout << "       range(start=2, length=100) got " << results.size() << " results\n";
    for (auto& [k, v] : results)
        std::cout << "           " << k << " -> " << v << "\n";

    check(results.size() == 2, "range stops at end of skiplist, returns 2 not 100");
    check(results[0] == std::make_pair(2, 20), "first result is (2, 20)");
    check(results[1] == std::make_pair(3, 30), "second result is (3, 30)");

    delete sl;
}

void test_range_key_not_in_list() {
    std::cout << "\n-- test_range_key_not_in_list --\n";
    auto* sl = new SimpleSkiplist<int, int>();

    sl->insert_or_modify(1, 10);
    sl->insert_or_modify(3, 30);
    sl->insert_or_modify(5, 50);
    sl->insert_or_modify(7, 70);
    std::cout << "       inserted (1,3,5,7), querying range starting at 4 (not in list)\n";

    std::vector<std::pair<int,int>> results;
    sl->range(4, [&results](int k, int v) {
        results.push_back({k, v});
    }, 2);

    std::cout << "       range(start=4, length=2) got " << results.size() << " results\n";
    for (auto& [k, v] : results)
        std::cout << "           " << k << " -> " << v << "\n";

    // should start from first key >= 4, which is 5
    check(results.size() == 2,          "range from missing key returns 2 results");
    check(results[0] == std::make_pair(5, 50), "first result is (5, 50) — first key >= 4");
    check(results[1] == std::make_pair(7, 70), "second result is (7, 70)");

    delete sl;
}

void test_range_start_beyond_all_keys() {
    std::cout << "\n-- test_range_start_beyond_all_keys --\n";
    auto* sl = new SimpleSkiplist<int, int>();

    sl->insert_or_modify(1, 10);
    sl->insert_or_modify(2, 20);
    std::cout << "       inserted (1->10),(2->20), querying range starting at 999\n";

    std::vector<std::pair<int,int>> results;
    sl->range(999, [&results](int k, int v) {
        results.push_back({k, v});
    }, 5);

    std::cout << "       range(start=999, length=5) got " << results.size() << " results\n";
    check(results.size() == 0, "range beyond all keys returns empty");

    delete sl;
}

void test_range_stress() {
    std::cout << "\n-- test_range_stress --\n";
    const int N = 500;
    auto* sl = new SimpleSkiplist<int, int>();

    for (int i = 0; i < N; i++)
        sl->insert_or_modify(i, i * 2);
    std::cout << "       inserted " << N << " elements (0-499)\n";

    // range starting at 100, length 50 — expect keys 100..149
    std::vector<std::pair<int,int>> results;
    sl->range(100, [&results](int k, int v) {
        results.push_back({k, v});
    }, 50);

    std::cout << "       range(start=100, length=50) got " << results.size() << " results\n";

    check(results.size() == 50, "stress range returns exactly 50 results");

    int order_failures = 0;
    int value_failures = 0;
    for (int i = 0; i < (int)results.size(); i++) {
        int expected_key = 100 + i;
        int expected_val = expected_key * 2;
        if (results[i].first != expected_key) {
            std::cout << "       [FAIL] result[" << i << "] key = " << results[i].first
                      << " expected " << expected_key << "\n";
            order_failures++;
        }
        if (results[i].second != expected_val) {
            std::cout << "       [FAIL] result[" << i << "] val = " << results[i].second
                      << " expected " << expected_val << "\n";
            value_failures++;
        }
    }
    check(order_failures == 0,  "all 50 results are in correct order");
    check(value_failures == 0,  "all 50 results have correct values");

    delete sl;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== SimpleSkiplist Tests ===\n";

    // test_get_on_empty();
    // test_insert_then_get();
    // test_get_missing_key();
    // test_modify_existing_key();
    // test_multiple_inserts_in_order();
    // test_multiple_inserts_out_of_order();
    // test_insert_at_boundaries();
    // test_destructor_on_empty();
    // test_destructor_on_full();

    // // // uncomment once you add remove()
    // test_delete_existing_key();
    // test_delete_then_reinsert();
    // test_replace();
    // test_delete_nonexistent_key();
    test_delete_one_of_many();
    // test_stress();
    // test_range_basic();
    // test_range_from_beginning();
    // test_range_length_exceeds_remaining();
    // test_range_key_not_in_list();
    // test_range_start_beyond_all_keys();
    // test_range_stress();

    std::cout << "\n=== Results: " << passed << " passed, " << failed << " failed ===\n";
    return failed == 0 ? 0 : 1;
}
