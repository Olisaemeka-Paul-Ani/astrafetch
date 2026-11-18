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

**Next sitting**
1. Run it several more times — one clean run proves little with threads.
2. Commit the fix (small, one feature).
3. Fix the test URLs: only `proof.ovh.net` works; print `response.error.message` / `status_code` on failure.
4. Snapshot method on the manager (rest of session 5's Done =).
