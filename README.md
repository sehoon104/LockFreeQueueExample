# Lock-Free Queue Benchmarks

A C++ benchmark suite comparing **lock-free** and **mutex-based** queue implementations under multi-threaded workloads.  
Includes SPSC (single-producer single-consumer) queues implementation, built from scratch to explore performance trade-offs in concurrent data structures.

---

## Implementations

| Queue Type | Description |
|-------------|-------------|
| **SPSCQueue** | Lock-free single-producer, single-consumer ring buffer. No CAS needed; minimal atomics. |
| **MutexBoundedQueue** | Same ring-buffer layout but protected by a single `std::mutex` for fairness comparison. |

All queues share the same buffer structure and `try_push` / `try_pop` API.

---

## Benchmark Overview

Each benchmark runs for **~10 seconds**, measuring total throughput and item count.

- **Producers:** generate integers continuously until stop signal.
- **Consumer:** drains the queue as fast as possible.
- **Result:** total produced/consumed items and throughput (million items/sec).

Running Lock-Free SPSC Queue benchmark, capacity=1048576, time=10s

Running Mutex-SPSC Queue benchmark, capacity=1048576, time=10s

### Example Output
```cpp
=== Results ===
Lock-free SPSC:
  time:       10.2046 s
  produced:   83128472
  consumed:   83128472
  throughput: 8.14617 M items/s
Mutex SPSC:
  time:       10.202 s
  produced:   59115215
  consumed:   59115215
  throughput: 5.79447 M items/s
