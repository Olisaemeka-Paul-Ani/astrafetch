# Session notes

Running log of where the project stands, so a fresh chat (or a fresh me) can pick up cold.
Spec and schedule live in `CLAUDE.md`.

## 2026-11-17 — session 5, first real run

**State**
- Build works in the Codespace (`cmake --preset default && cmake --build build`, then `./build/astrafetch`).
  The preset now reads `$VCPKG_ROOT`; on Windows set `VCPKG_ROOT=C:\vcpkg` as an env var.
- `DownloadManager` (src/manager.h): 3 worker threads + FIFO queue guarded by a mutex, workers sleep on a
  condition variable. **Confirmed working:** 3 downloads run, the rest wait, a new one starts each time one ends.
- Commit `ca2fd42` claims a "confirmed" shutdown hang — that was a prediction; main() had never run before today.
  The hang turned out to be real (see below).

**Bug found — program never exits** (fixed 2026-11-18, see below)
- Last output: `Destructor: about to join workers`, then nothing. First `join()` never returns.
- Cause (understood via the "doorbell" analogy): an idle worker sleeps in `getDownload()`'s
  `while (q.empty()) cv.wait(lock);`. On shutdown `notify_all()` wakes it, it re-checks only "is the queue empty?",
  sees yes, and goes back to sleep. Nothing in that loop ever asks "are we shutting down?"
- **Not yet fixed — design is mine to do:** what should the worker check on waking, and what should
  `getDownload()` return when shutting down so `workerLoop()` knows to exit?

**Other things noticed**
- 6 of 8 test URLs fail. Only `https://proof.ovh.net/files/100Mb.dat` succeeded. `speed.hetzner.de` looks dead.
  Print `response.error.message` / `status_code` in the failure branches; pick 1-2 known-good URLs (CLAUDE.md risk #5).
- Output lines from different threads get mixed together on one line — `std::cout` is shared too.

## 2026-11-18 — shutdown hang fixed

**The fix (mine)**
- Destructor: set `running = false` **before** `cv.notify_all()`, so a worker that finishes a download after the
  notify sees `false` at `while (running)` and exits instead of going back to sleep with no one to wake it.
- `getDownload()`: right after `cv.wait(lock)`, inside the `while (q.empty())` loop, check `running`; if false,
  return a null `shared_ptr` (`nullptr`). After the loop the queue is guaranteed non-empty (the only other way out
  of the loop), so front/pop/return needs no extra check.
- `workerLoop()`: if `getDownload()` returns `nullptr`, return (thread ends, `join()` completes) — checked
  **before** any `d->` call.
- First run: exit code 0, 3× "Worker thread records empty download", then Worker 1/2/3 "has shut down".

**What clicked (the concepts)**
- `cv.wait()` is a **blocking call**: the thread sleeps *inside* it and, when woken, continues on the very next
  line — it does not jump back to `workerLoop()`. So whatever a woken worker must notice has to be checked in the
  wait loop itself.
- `notify_all()` only wakes threads **currently waiting** on the cv. Busy threads are unaffected, and a
  notification with nobody waiting is lost — not saved for later.
- `join()` doesn't stop or kill a thread; it makes the *caller* wait until the thread returns on its own.
- `detach()` is wrong here: the destructor would finish, the manager's members (`m`, `cv`, `q`, even `running`)
  get destroyed, and a still-running worker would use freed memory (undefined behavior).
- During the destructor *body*, all members are still alive — that's why joining there is safe.
- Words: threads *wait on* a cv, they *hold* a mutex; a mutex is the object, a lock (`unique_lock`) is the RAII
  object that locks it; "atomic" describes operations on data, not functions like `join()`.

**Next sitting** (updated below)

## 2026-11-18 (later) — snapshot

**Done (mine)**
- Shutdown fix verified over 5 repeated runs, committed and pushed.
- `DownloadManager::snapshotEntry` (public struct: `id`, `url`, `status`, `dlSoFar`, `dlTotal`, byte counts as
  `cpr::cpr_off_t` — an `int` would overflow past ~2 GB).
- `Download::getSnapshot()` now also returns `url`, so the manager gets every field from one locked read.
- `DownloadManager::getSnapshot()`: locks `mapMutex`, loops `store`, builds a `std::vector<snapshotEntry>`, returns it.

**Concepts**
- Snapshot = read-only copy, taken under the lock. Reading (incl. the `id`) happens through it; acting
  (`pause(id)`, `cancel(id)`) happens through manager methods. The id is in the snapshot so the actor knows *which*.
- Postfix operators (`.` `->` `()` `[]`) bind tighter than prefix (`*` `&` `!`): `*p.f()` is `*(p.f())`. Use `p->f()`.
- `std::unique_lock` can `unlock()` early (and relock); `std::lock_guard` can't. `cv.wait` needs `unique_lock`.
- Without the lock in the snapshot: an `addDownload()` insert can rehash the `unordered_map` mid-loop → the loop's
  iterator points at freed memory → undefined behavior. A mutex only protects data if *every* accessor locks it.

**Open question — understand later**
- Deadlock: two threads each holding one mutex and blocked in `lock()` waiting for the other's. Neither reaches its
  unlock, because locking is a blocking call. Consistent lock ordering prevents it.
- My code: the snapshot takes `mapMutex` → then `Download::m`. **Check: does anything take them in the opposite
  order?** (Answer this myself.)

**Next sitting**
1. Answer the lock-order question above.
2. Console uses the snapshot: `main.cpp` calls `manager.getSnapshot()` and prints rows while downloads run
   (last item of session 5's Done =).
3. Be able to explain, out loud: why the queue needs a mutex but the byte counter can be an atomic.
4. Fix the test URLs: only `proof.ovh.net` works; print `response.error.message` / `status_code` on failure.
