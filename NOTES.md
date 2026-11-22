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

## Deadlocks — study notes + self-check

**Definition.** A deadlock is when two (or more) threads are each blocked waiting for something that only another
blocked thread can give up — so none of them ever proceeds. Nothing crashes, nothing errors; the program just stops.

**The classic case — two mutexes, opposite order:**
```
Thread 1                          Thread 2
lock(A)   ✓ holds A
                                  lock(B)   ✓ holds B
lock(B)   ✗ waits for T2…
                                  lock(A)   ✗ waits for T1…
```
**Why they don't "eventually release":** a thread releases a mutex only when its code *reaches* the unlock (end of
scope for `lock_guard`/`unique_lock`, or `.unlock()`). `lock()` is a **blocking call** — the thread is paused
*inside* it and runs no further code, including its unlock. T1 can't release A until it gets B; B is only released
when T2 gets A. Circular wait → forever.

**All four have to be true for a deadlock** (break any one and it can't happen):
1. *Mutual exclusion* — a mutex can be held by only one thread at a time.
2. *Hold and wait* — a thread holds one mutex while waiting for another.
3. *No preemption* — nobody can take a mutex away from the thread holding it.
4. *Circular wait* — T1 waits on T2, T2 waits on T1 (or a longer cycle).

**How to prevent it (in order of what I'll actually use):**
- **Consistent lock ordering** — every place in the codebase that holds two mutexes takes them in the same order.
  Breaks *circular wait*. This is the CLAUDE.md rule. Write the order down once and stick to it.
- **Hold only one mutex at a time** where possible — e.g. `addDownload()` unlocks `m` *before* taking `mapMutex`.
  Breaks *hold and wait*.
- **Keep critical sections small** — don't call unknown/other-locking functions, do I/O, or wait on the network
  while holding a mutex.
- **`std::scoped_lock lk(A, B);`** (C++17) — locks several mutexes together using a deadlock-avoiding algorithm,
  so order doesn't matter at that call site.

**Other ways to deadlock (same idea, one thread or no second mutex):**
- **Locking the same `std::mutex` twice in one thread** — e.g. a method that holds `m` calls another method that
  also locks `m`. The thread waits for itself, forever. (`std::mutex` is not re-entrant.)
- **`join()`ing a thread that is waiting for you** — e.g. holding a mutex the worker needs, then calling `join()`.
- **A condition variable nobody will ever notify** — what happened in session 5's shutdown bug. Strictly that's a
  "lost wake-up"/hang rather than a lock-cycle deadlock, but it looks identical from outside.

**What it looks like when it happens:** output stops, the program never exits, CPU ~0% in `top` (threads are
asleep, not spinning). To see where each thread is stuck: run under `gdb`, Ctrl+C, then `thread apply all bt` —
the stuck threads will be sitting in `lock()` / `wait()`. Compiling with `-fsanitize=thread` also reports
**lock-order inversion** even on runs where the deadlock didn't actually happen.

**Self-check — answer these without looking back up:**
1. In one sentence: what is a deadlock?
2. Why can't T1 just release A while it waits for B?
3. Name the four conditions. Which one does consistent lock ordering break? Which does "hold one mutex at a time" break?
4. If every thread always locks A before B, walk through why the circular wait above can't form.
5. What happens if a method holding `std::mutex m` calls another method that also locks `m`?
6. A program hangs at 0% CPU. Deadlock or infinite loop — and how would you tell?
7. **In AstraFetch:** list every place that holds two mutexes at once and the order it takes them. Does any pair
   appear in both orders? (`getSnapshot()` takes `mapMutex` → `Download::m`.)

**Drill (like session 4's race drill — throwaway, outside the repo):** write a toy program with two mutexes and two
threads that lock them in opposite orders, each with a short `sleep_for` between the two locks. Run it and watch it
hang. Then fix it twice: once by making both threads use the same order, once with `std::scoped_lock`.

## 2026-11-22 — deadlocks, worked through piece by piece

**What I can now explain myself**
- `lock()` on a held mutex: the thread blocks (sleeps); it continues only after the holder releases *and* the OS
  schedules it. With several waiters, one gets it per unlock; which one is **not guaranteed** (a non-waiting thread
  can even barge in first). Unlucky thread that keeps losing = starvation.
- **A thread keeps every mutex it holds until its own code reaches the unlock** — whether preempted, blocked on
  another mutex, in `join()`, or in `sleep_for`. Only exception: `cv.wait` releases its mutex on purpose.
- Why `cv.wait` must release: if a worker slept holding the queue mutex, `addDownload()` could never lock it to
  push → nobody notifies → worker and adder each wait on the other forever. (Found that deadlock myself.)
- **Chain vs. cycle:** T3→T1→T2→(nothing) unwinds; T1→T2→T1 never does. **A deadlock is a cycle of waiting.**
- It's **timing-dependent**: opposite-order code is fine whenever one thread gets *both* mutexes before the other
  gets its first; it hangs only in that window. Works 999 times, hangs on the 1000th.
- Can't fix it by controlling preemption (impossible, and on 2 cores there's no preemption at all).
  Fix = **consistent lock ordering**: then a thread waiting for B can only hold A, so every wait points *forward* —
  no loop back, no cycle, under any schedule.
- **Mutexes are held by threads, not classes.** Calling into another class's locking method from inside a critical
  section means *the same thread* holds both. Indirect locks are where deadlocks hide.

**AstraFetch lock-order check (self-check #7) — answered**
- Only `DownloadManager::getSnapshot()` holds two at once: `mapMutex` → `Download::m` (via `dwnl->getSnapshot()`).
- `addDownload()` releases the queue mutex before taking `mapMutex` — never holds both.
- `getDownload()` and all `Download` methods hold one mutex only.
- One order, used in one place → no cycle possible. ✓
- Naming: kept both mutexes as `m` (separate classes) — my call.

## 2026-11-22 — session 5 DONE

**Console reads the snapshot (mine):** `main()` loops for 30s (`steady_clock` elapsed time), each pass takes one
`manager.getSnapshot()`, prints every row, then `sleep_for(2s)` — ~15 overviews. Verified: percentages climb.
- Bug I hit: `sleep_for` inside the per-row loop → 2s *per row* → only 2 overviews in 30s, and rows were stale
  by the time they printed. Fix: sleep once per snapshot, after printing all rows.
- Bug I hit: `dlSoFar / dlTotal * 100` with integers → integer division truncates → only 0% or 100%.
  Fix: `dlSoFar * 1.0 / dlTotal * 100`. (`cpr::cpr_off_t` = 64-bit signed integer, "offset type".)
- Sleep in the display loop = pacing; without it: terminal flood, a core at 100%, constant `mapMutex` contention.
  A sleeping thread is *blocked* (not preempted) — the scheduler gives the CPU to runnable threads (workers).
- `.` accesses an object's member; `::` accesses something inside a class/namespace (`DownloadManager::snapshotEntry`).

**The session-5 explanation, in my words (refined):**
> A byte counter **can** use an atomic, because changing the value of a primitive **can** occur in one
> uninterruptible step using a special CPU instruction — `std::atomic` is what makes the compiler use that
> instruction; a plain `int` doing `++` is still three steps (read, add, write). A queue is a complex object, not a
> primitive: a single `push` or `pop` changes several internal pieces (pointers, size, memory), so no single
> instruction can do it — a mutex has to be used to protect **the queue** so **other threads** can't access it
> mid-modification. A mutex also covers a **sequence** of operations — checking `empty()` and then `front()` and
> `pop()` must happen together — which an atomic can't.

**Build stages:** configure (`cmake --preset default`, once) → build (`cmake --build build` = compile each changed
.cpp → link with cpr/curl/OpenSSL into the executable) → run. Compile errors vs. "undefined reference" link errors.

**Next sitting — session 6:** per-download pause/cancel, basic retry, console command loop
(`add <url>`, `list`, `pause <id>`, `resume <id>`, `cancel <id>`, `quit`). The command loop replaces the 30s timer.
Still open: fix the test URLs (only `proof.ovh.net` works; print `response.error.message` / `status_code`).
