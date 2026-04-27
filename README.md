# Concurrent Skiplist

Implementing and studying concurrent skiplists, working toward a replication of the B-Skiplist paper:
> *Bridging Cache-Friendliness and Concurrency: A Locality-Optimized In-Memory B-Skiplist*
> https://arxiv.org/abs/2507.21492

Skiplists written by hand. Tests and benchmarking written with AI assistance.

---

## Implementations

### `src/simple_skiplist/`
A single-threaded skiplist with no locking. Baseline implementation to get the data structure right before adding concurrency.

### `src/concurrent_skiplist_lock/`
Concurrent skiplist using a hand-rolled spinlock (custom `Lock` class backed by `std::atomic<bool>`). Uses hand-over-hand (HOH) locking — at most two locks held at a time as the traversal moves right or down. All operations (get, insert, remove, range) are protected by exclusive locks.

### `src/concurrent_skiplist_read_write_mutex/`
Same HOH locking structure, but upgrades the spinlock to `std::shared_mutex` to allow multiple concurrent readers. Read operations (`get`, `range`) take shared locks; writes (`insert_or_modify`, `remove`) take exclusive locks. The promotion level of an insert determines where the transition from shared to exclusive locking happens during traversal.

### `src/cache-friendly/`
Implementation of the B-Skiplist from the paper. Key differences from a standard skiplist:
- Each node holds a fixed-size sorted **array** of key-value pairs (controlled by `ARRAY_SIZE`) rather than a single element, improving cache locality
- Each array element has its own `down` pointer (per-element down pointers, not per-node)
- Nodes are split preemptively on the way down during insert, so no backtracking is needed
- Uses `std::shared_mutex` per node for reader-writer concurrency

---

## Running the tests

Each implementation has its own test files. Compile and run from the relevant directory.

**Simple skiplist**
```sh
cd src/simple_skiplist
g++ -std=c++20 -o test_skiplist test_simple_skiplist.cpp
./test_skiplist
```

**Concurrent skiplist (spinlock)**
```sh
cd src/concurrent_skiplist_lock
g++ -std=c++20 -pthread -o test_skiplist test_concurrent_skiplist.cpp
./test_skiplist
```

**Concurrent skiplist (read-write mutex specific tests)**
```sh
cd src/concurrent_skiplist_read_write_mutex
g++ -std=c++20 -pthread -o test_skiplist test_rw_mutex.cpp
./test_skiplist
```

---

### What the metrics mean

Each row reports two throughput numbers:

- **load (Mops/s)** — million inserts per second during the initial population phase. All `NUM_KEYS` keys are inserted into an empty structure before any workload runs. This measures pure insert throughput with no read/write contention.
- **run (Mops/s)** — million operations per second during the actual workload phase. After loading, `OPS_PER_BENCH` operations are issued according to the workload type (reads, writes, or a mix). This is the more realistic number.

The two phases are kept separate because inserts behave differently on an empty structure (no equal-key path, more predictable splits) versus a fully populated one.

### Results (Apple M-series, 500K keys, 1M ops, ARRAY_SIZE=5, MAX_HEIGHT=20)

```
        workload   threads   load (Mops/s)   run  (Mops/s)
----------------------------------------------------------

      write-only         1           0.754           0.544
      write-only         2           1.226           0.958
      write-only         4           1.571           1.585
      write-only         8           0.715           0.845

       read-only         1           0.731           0.993
       read-only         2           1.157           1.230
       read-only         4           1.312           1.219
       read-only         8           0.720           0.713

     mixed-50/50         1           0.749           0.614
     mixed-50/50         2           1.168           1.053
     mixed-50/50         4           1.565           1.743
     mixed-50/50         8           0.680           0.758

      read-heavy         1           0.736           0.714
      read-heavy         2           1.135           1.203
      read-heavy         4           1.596           1.667
      read-heavy         8           0.771           0.833

            scan         1           0.732           0.758
            scan         2           1.134           0.989
            scan         4           1.318           1.278
            scan         8           0.679           0.661
```

### Observations

- **Throughput peaks at 4 threads** then drops at 8. Apple Silicon chips typically have 4 performance cores; beyond that, threads compete for the same cores and lock contention outweighs parallelism gains.
- **Writes scale well up to 4 threads** despite needing exclusive locks, because HOH locking limits contention to two nodes at a time.
- **Reads scale slightly less than writes** in some workloads — `shared_mutex` on Apple Silicon has non-trivial overhead even for shared acquisition.
- **Mixed workloads track write performance** since writers hold exclusive locks and block all concurrent readers on the same node.

> **Note:** `MAX_HEIGHT` must be set to approximately `log₂(NUM_KEYS)` for O(log n) traversal. With the default `MAX_HEIGHT=5` the structure holds at most ~3125 keys efficiently; beyond that the top-level list grows linearly and throughput collapses.
