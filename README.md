# ⚡ KVStore — In-Memory Key-Value Store in C++

> A multithreaded, in-memory key-value store inspired by Redis.  
> Built from scratch in C++17 with TCP networking, TTL expiration, and LRU eviction.

---

## Architecture

```
Client (nc / telnet / custom client)
        │
        │  TCP Socket (port 6379)
        ▼
┌─────────────────────────────────────────┐
│              TCP Server                 │
│   accept() → spawns thread per client  │
└────────────────┬────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────┐
│           KeyValueStore                 │
│                                         │
│  ┌─────────────┐   ┌─────────────────┐  │
│  │ unordered_  │   │   LRU List      │  │
│  │    map      │◄──│ (doubly linked) │  │
│  └─────────────┘   └─────────────────┘  │
│                                         │
│  ┌──────────────────────────────────┐   │
│  │  shared_timed_mutex              │   │
│  │  (concurrent reads, safe writes) │   │
│  └──────────────────────────────────┘   │
│                                         │
│  ┌──────────────────────────────────┐   │
│  │  Reaper Thread (TTL cleanup)     │   │
│  │  wakes every 1s, evicts expired  │   │
│  └──────────────────────────────────┘   │
└─────────────────────────────────────────┘
```

---

## Features

| Feature | Details |
|---|---|
| **TCP Server** | POSIX sockets, port 6379 (Redis default) |
| **Multithreaded** | One thread per client, detached |
| **Thread Safety** | `shared_timed_mutex` — concurrent reads, exclusive writes |
| **TTL Expiration** | Per-key expiry via `std::chrono`, background reaper thread |
| **LRU Eviction** | O(1) eviction using doubly linked list + hashmap |
| **Commands** | SET, GET, DEL, EXISTS |

---

## Commands

```
SET key value           → stores key with no expiry
SET key value <ttl>     → stores key, expires after <ttl> seconds
GET key                 → returns value or (nil) if missing/expired
DEL key                 → deletes key
EXISTS key              → returns 1 (exists) or 0 (missing/expired)
```

---

## Build & Run

**Requirements:** C++17, CMake 3.15+, Linux or macOS

```bash
git clone https://github.com/yourusername/kvstore.git
cd kvstore
mkdir build && cd build
cmake ..
make
./kvstore
```

Server starts on **port 6379**.

---

## Connect & Test

Open a second terminal:

```bash
nc localhost 6379
```

Try it live:

```
SET name alice
OK

GET name
alice

SET token abc123 5
OK

GET token
abc123

# wait 6 seconds...

GET token
(nil)

DEL name
OK

EXISTS name
0
```

Open multiple terminals simultaneously — all clients are handled concurrently.

---

## File Structure

```
kvstore/
├── CMakeLists.txt
├── include/
│   ├── store.h          # KeyValueStore class — TTL, LRU, mutex
│   └── server.h         # TCP server — parse, handle_client, setup_server
├── src/
│   ├── main.cpp         # Entry point
│   ├── store.cpp        # KV store implementation
│   └── server.cpp       # TCP server implementation
└── tests/               # (coming soon)
```

---

## Design Decisions

**Why `shared_timed_mutex`?**  
GET operations (reads) can run concurrently — no need to block each other. Only SET/DEL/eviction need exclusive access. This gives significantly better read throughput under concurrent load.

**Why `std::list` + `unordered_map` for LRU?**  
The hashmap gives O(1) key lookup. The doubly linked list gives O(1) node reordering via `splice()`. Storing iterators in the map means we never search the list — every operation is O(1).

**Why a background reaper thread instead of lazy deletion?**  
Lazy deletion (checking TTL only on GET) leaves expired keys in memory. A background reaper guarantees bounded memory usage, which matters under high write load with short TTLs.

**Why `std::thread::detach()` per client?**  
Simple and effective for a demo-scale server. Each client thread owns its own socket and cleans up on disconnect. A production system would use a thread pool to bound resource usage.

---

## Concurrency Model

```
Multiple clients reading simultaneously:
  Thread A ──shared_lock──► GET "name"  ✅
  Thread B ──shared_lock──► GET "age"   ✅  (both proceed)

One client writing — blocks all readers:
  Thread A ──unique_lock──► SET "name" "bob"
  Thread B ─────────────── WAITS until A releases lock
```

---

## What's Next

- [ ] Thread pool (bound max threads)
- [ ] Persistence — dump to disk on shutdown
- [ ] RESP protocol (real Redis wire format)
- [ ] Benchmark vs Redis with `redis-benchmark`

---

## Built With

- C++17
- POSIX Sockets
- `std::thread`, `std::shared_timed_mutex`, `std::atomic`
- `std::chrono` for TTL
- `std::list` + `std::unordered_map` for LRU
