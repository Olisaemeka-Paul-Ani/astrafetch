# AstraFetch — CLAUDE.md

A concurrent, segmented HTTP download manager in C++ — a reduced-scope MVP across **16 sessions in 7 weeks**, at roughly one 1-2 hour session per day, most days. This is a genuinely reduced-scope MVP, not the original full spec — the cuts below were decided deliberately, up front, based on an honest feasibility assessment done before writing any code. See "Feasibility & scope decisions" for why.

**Scope decision, settled:** the Dear ImGui GUI is in scope, as sessions 12-14, and only after the console engine and the segmented download path are stable. It is also the first thing cut if the session 11 checkpoint says you're behind. A half-wired window on top of a working engine is worse than no window.

## Non-negotiables

- Language: C++17 or C++20. Package management: `vcpkg` from day 1 — don't hand-roll linking, this is a classic Windows time sink.
- HTTP/HTTPS: `cpr` (ergonomic wrapper) or `libcurl` directly if finer control over Range-request streaming callbacks is needed. Never write raw HTTP or handle TLS yourself — let the library own it.
- JSON persistence: `nlohmann::json` — header-only, minimal learning curve.
- GUI: Dear ImGui — immediate-mode, far shallower learning curve than Qt. GUI work does not start until the console engine and segmentation are solid (see critical path). No other GUI framework enters this repo.
- Logging: `spdlog`, genuinely optional — `std::cerr` is fine if time is tight.
- Testing: `Catch2`, single-header. Used only for pure-logic functions (e.g. byte-range splitting math) — not for threading/networking itself, which is far more effectively verified by running the program.
- Concurrency: prefer `std::atomic` for simple shared counters (sidesteps needing a mutex for the common case); one mutex per job's more complex state, with consistent lock ordering across the codebase.
- The checkpoint cuts below (see "Checkpoints") are decided now, not renegotiated under deadline pressure later.

## Feasibility & scope decisions (why this spec looks the way it does)

Assessed before writing any code, honestly:

- Finishing the **full original scope** (4-8 concurrent segments, full reliability matrix, full test/logging/benchmark suite, polished GUI): **under 10% probability** — for someone who already knows STL, threading, and networking, that's a 3-4 week full-time job. Learning those simultaneously makes the full scope unrealistic, not because of lack of capability, but because the learning load alone consumes a large share of available hours before project-specific code even starts.
- Finishing the **MVP defined below**, console engine plus a basic GUI: 55-65% likely, *if* scope is cut aggressively and early rather than under pressure at the end. The console engine alone (sessions 1-11) is 70-75% likely.
- Realistic quality bar: a working app doing correct concurrent and segmented downloads with real synchronization, with a basic live GUI on top — not a polished consumer product. That is a legitimate, respectable outcome at this experience level.
- Plan for 1-2 hours per sitting, most days. Expect some sittings where an hour of concurrency debugging produces almost no visible progress. That's normal for this category of work, not a sign of falling behind. Sessions below are units of work, not units of time; a "session" may take two or three sittings.

**Learning curve, easiest → hardest:** STL basics (`vector`/`string`/`map`) → basic file streaming (`ofstream`) → RAII as a concept → JSON persistence via a library → basic HTTP GET via a library → basic `std::thread` (spawn/join) → HTTP Range requests conceptually → **synchronization** (mutexes, condition variables, avoiding races/deadlocks — hard, and hard to *debug* since bugs are non-deterministic) → **GUI framework integration** (a separate learning curve from C++ itself) → **segmented downloads + pause/resume + retry combined** (threading, sync, networking, and file I/O all interacting at once) → **thread-safe GUI updates from background threads** (hardest, combines the two hardest items above).

What needs deep understanding: basic mutex/atomic usage patterns, since they'll be used constantly. What only needs "just enough to use safely": TLS (never touch directly), CMake (just enough to add dependencies via vcpkg), GUI framework internals (treat as a black box).

## Theory checkpoints — understand these, don't just call the library function

Using `cpr`/`libcurl`/`std::thread` correctly is not the same as understanding what they're doing underneath. Each of these should be explainable in your own words, not just usable, before moving past the session that introduces it:

- **HTTP request/response model** (session 1): a request has a method (GET), a URL, and headers; a response has a status code (200 OK, 404, etc.), headers, and a body. The library sends bytes over a TCP connection and parses the reply back into that shape — it's not magic, it's a text-based protocol you could read by hand if you had to.
- **Streaming to disk vs. loading fully into memory** (session 2): why a large file shouldn't be held entirely in a `string`/`vector` before writing — a write callback lets you flush chunks to disk as they arrive instead.
- **RAII** (sessions 1-2 onward): why a class's constructor acquiring a resource (a file handle, a curl handle) and its destructor releasing it means you can't forget to clean up, even if an exception is thrown partway through.
- **Threads vs. concurrency vs. parallelism** (session 4): a thread is an independent sequence of execution sharing your process's memory; running multiple downloads on threads is concurrency (progress interleaves) whether or not it's true parallelism (simultaneous, on separate CPU cores) — for I/O-bound work like network downloads, the benefit comes from not blocking on one slow connection while others could be progressing, not from raw CPU parallelism.
- **Race conditions, mutexes, and atomics** (session 5): a race condition is two threads reading/writing the same memory without coordination, where the outcome depends on timing — genuinely non-deterministic, which is exactly what makes it hard to debug. A `mutex` makes one thread wait its turn before touching shared data; an `std::atomic` is a lighter-weight tool for simple values (a counter, a flag) where the hardware itself guarantees the read-modify-write happens as one indivisible step, no waiting required.
- **HTTP Range requests** (session 8): a client can ask a server for only bytes 1000-1999 of a file via a `Range` header; a server that supports it replies `206 Partial Content` instead of `200 OK`. That's the entire mechanism segmented downloading rests on — several Range requests for different byte spans of the same file, running concurrently, then concatenated back together in order.
- **Cooperative cancellation** (session 10): a thread in the middle of a blocking network read can't be safely killed from outside. Cancellation works by *asking* — setting an atomic flag the worker checks between chunks, then letting the worker unwind and clean up its own temp file. Understand why "just kill the thread" is not an option.
- **Why the GUI thread never touches worker-thread data directly** (session 13): a GUI redraw happens dozens of times a second on its own thread; if it read a `DownloadManager`'s state without synchronization while a worker thread was mid-write, that's the same race condition as above, just harder to spot because it shows up as visual glitching instead of a crash.

If a session's "Done =" box is checked but you can't explain the corresponding concept above out loud, that session isn't actually finished — go back before moving on.

## Concurrency drill — deliberately reinforced, not introduced once and left

Concurrency is the hardest thing in this project and the easiest to fake-understand by copying a working pattern without knowing why it works. Realistic goal, stated honestly: solid *working* understanding (correctly identify and fix a race condition, explain mutex vs. atomic tradeoffs, reason about thread safety in a real program) — not deep/formal mastery (memory ordering, lock-free structures, condition-variable-heavy designs). That deeper level is a genuinely separate, longer study track, out of scope here.

Four deliberate passes at the same reasoning, in increasingly real contexts:

- **Session 4, before touching the download manager:** write a small, throwaway toy program (not part of the AstraFetch source tree) — spawn several threads that each increment one shared `int` a large number of times with *no* synchronization at all, and print the final count. It won't match the expected total. That's the race condition, made visible and undeniable, not just described. Then fix it once with a `std::mutex`, then again with `std::atomic<int>`, and compare. This is a warm-up, the same way session 1's STL practice is, not a new AstraFetch feature.
- **Session 5:** before writing the real manager's shared progress state, explain out loud (or in a comment) which specific variables are shared across threads and why each one needs a mutex vs. can be a plain atomic — don't let library/pattern copying substitute for this.
- **Session 9:** the segment-resume work reintroduces shared state (offsets, completion flags) under harder conditions (interacting with file I/O and networking at once) — round two: same "which variable, why this tool" reasoning, applied to a messier case.
- **Session 10:** cancel or pause arriving *mid-segment* is round three. A single cancel has to tear down several in-flight Range requests and their temp files without leaking, double-freeing, or leaving a half-written file behind.
- **Session 13:** GUI/worker interaction is round four — same reasoning again, applied to "the GUI thread only ever reads, never blocks on a worker."

One introduction on session 4 alone will not make this stick.

## Feature classification

**MUST HAVE:** HTTP/HTTPS download-to-disk via a library, progress tracking, cancel, basic error handling (bad URL, connection failure), 2-3 downloads running concurrently (whole-file, threaded), console UI.

**SHOULD HAVE:** Download manager (add/pause/cancel/remove), JSON persistence of the download list and status across restarts, segmented download via Range requests as one demonstrated case, basic retry, basic Dear ImGui interface.

**NICE TO HAVE:** Real byte-offset resume-after-restart; logging; any GUI polish beyond list + progress bars + buttons.

**CUT entirely:** 4-8 concurrent segments as a general system; full reliability matrix; full unit+integration test suite; Qt or any heavy GUI framework; download priorities/complex queueing; graceful fallback for every server misbehavior; a performance-obsessed benchmark suite (one clean benchmark is enough).

## The realistic version, concretely

- **Core:** HTTP download to disk via `cpr` (or `libcurl` directly if Range-request control is needed)
- **Concurrency:** 2-3 concurrent *whole-file* downloads via `std::thread` — not per-segment concurrency as the default mode
- **Segmented downloading:** implemented once, as a focused demo feature (split one file into 2-3 Range-request segments, download concurrently, concatenate) — not the default path for every download
- **Pause/resume:** pause stops the thread cleanly via an atomic flag; resume re-issues a Range request from the last known byte offset
- **Cancellation:** cooperative, and correct mid-segment — no orphaned temp files, no crash
- **Persistence:** JSON file (`nlohmann::json`) with the download list and status; full segment-level resume-from-disk is a stretch goal, not a requirement
- **Console UI:** `add <url>`, `list`, `pause <id>`, `resume <id>`, `cancel <id>`, `quit`. This is the primary interface and it stays working even after the GUI exists.
- **GUI:** Dear ImGui — list, progress bars, add/pause/cancel buttons, nothing fancier. Reads manager state each frame, never writes to worker-owned data.
- **Reliability:** handle the obvious failures (bad URL, connection drop, disk write error) with clear status reporting — don't chase every edge case
- **Testing:** 2-3 Catch2 unit tests on pure logic (e.g. byte-range splitting math), not on threading/networking itself

## Suggested file structure

```
astrafetch/
├── CMakeLists.txt       # vcpkg-integrated build config
├── README.md            # what was built, what was cut and why, benchmark numbers
├── src/
│   ├── main.cpp         # entry point, argument parsing, starts console or GUI loop
│   ├── downloader.cpp/.h    # single-download logic: HTTP GET/Range, streaming to disk, progress callback
│   ├── manager.cpp/.h       # DownloadManager class: owns threads, shared state, add/pause/cancel/retry
│   ├── segmenter.cpp/.h     # segmented-download demo feature: splits into Range-request chunks, concatenates
│   ├── persistence.cpp/.h   # nlohmann::json load/save of the download list + status
│   └── gui.cpp/.h           # Dear ImGui window: renders the manager's snapshot, buttons call manager methods.
│                            # Contains NO logic — deleting this file must leave a working program.
└── tests/
    └── test_segmenter.cpp   # Catch2 tests on pure logic (e.g. byte-range splitting math)
```

Adjust freely once real code exists — this is a starting shape, not a fixed contract.

## Critical path

```
URL → HTTP GET (library) → write-callback streams to disk → progress tracked
    → wrapped in a thread (non-blocking) → managed alongside other threads (manager)
    → split into Range-request segments → merge into final file
    → pause (stop thread, save offset) / resume (Range from offset) / cancel mid-segment
    → persist state to JSON
    → GUI polls manager state, renders
```

Key dependency: **GUI comes last, and that is not negotiable mid-project.** Debugging threading bugs through a GUI is far harder than through console prints — a race that crashes the program gives you a stack trace, the same race in a render loop gives you a flickering progress bar and nothing else. Don't touch ImGui until the segmented path survives repeated runs. Segmentation in turn depends on a working threaded manager (sessions 8-10 depend on 4-6, not the reverse).

## Stop-anywhere design

The GUI may be cut at session 11. The architecture is built so that stopping there produces a finished project, not an abandoned one — and so that continuing requires no refactor. Three rules, all of which apply from session 5 onward, long before any GUI exists:

- **The manager exposes a snapshot, not its internals.** One method returns a copy of the current state of every download (id, url, bytes done, total, status), taken while holding the lock. The console prints that snapshot. The GUI, later, renders the same snapshot. Neither ever reaches into the manager's members directly. A **snapshot** is a copy of the data taken at one instant under the mutex, so the reader works from a frozen picture instead of live memory another thread is still writing to. That is the entire reason a render loop can read manager state 60 times a second without racing.
- **No logic ever lives in `gui.cpp`.** Every action the GUI can trigger already exists as a manager method that the console calls first. `gui.cpp` is buttons calling `manager.pause(id)` and nothing else. If the file is deleted, the program still builds and still works.
- **The README is written at session 11 as if it were final** — benchmark numbers, architecture note, what was cut and why. If the project stops there, it is done that day, not "done except for the write-up." If it continues, session 15 only adds screenshots and a GUI paragraph.

This is the difference between stopping cleanly and abandoning. Do not defer any of the three to "when I know whether I'm continuing."

## 7-week schedule (16 sessions, ~2-3/week, 1-2 hrs each)

| Week | Session | Learn (theory, see checkpoints above) | Build | Done = |
|---|---|---|---|---|
| 1 | 1 | STL (`vector`/`string`), RAII concept, vcpkg setup, **HTTP request/response model** | Basic `cpr`/`libcurl` GET, print response size | Compiles, fetches a URL's content into memory, and you can explain what request/response actually means |
| 1 | 2 | `ofstream` basics, **streaming vs. loading fully into memory** | Stream response body directly to disk | Real file downloaded to disk, correct byte count |
| 1 | 3 | — | Progress callback + cancel flag | Single download, console progress %, cancelable |
| 2 | 4 | `std::thread` basics, **threads vs. concurrency vs. parallelism** + toy race-condition drill | Move one download onto a background thread | Download runs off main thread, program stays responsive, and you can explain why this helps for I/O-bound work specifically |
| 2 | 5 | `mutex`/`atomic` basics, **race conditions, conceptually** | Download manager class, 2-3 concurrent whole-file downloads, shared progress state, **snapshot method** (see Stop-anywhere design) | Can queue multiple URLs, concurrent, no crashes/races under casual testing, console reads state only via the snapshot, and you can explain what would go wrong without the mutex/atomic |
| 2 | 6 | — | Per-download pause/cancel, basic retry, console command loop | Can pause/cancel each download individually via console commands |
| 3 | 7 (checkpoint) | `nlohmann::json` | Persist download list + status to disk, restore on startup | Multi-threaded manager + persistence, all console-driven — see checkpoints below |
| 3 | 8 | HTTP Range requests, **byte-serving/partial content, conceptually** | Segmented download for ONE file (2-3 segments, separate temp files, concatenate) | One file downloads faster via segments, final file verified byte-identical, and you can explain what a `Range` header and `206` response actually mean |
| 3 | 9 | — | Segment-level resume (Range from last offset) — hardest session so far, expect it to slip | Best effort; if not solid, apply the Week 4 fallback below |
| 4 | 10 | **cooperative cancellation** | Cancel/pause arriving mid-segment: tear down in-flight Range requests, clean up temp files | Cancel during a segmented download leaves no orphaned temp files, no crash, repeatable 10 times in a row |
| 4 | 11 (checkpoint) | — | Debug/stabilize sessions 8-10, run repeatedly, consider `-fsanitize=thread`; **write the README in final form** (benchmark, architecture note, what was cut and why) | Segmented path survives repeated runs without intermittent failures, and the repo is shippable as-is today. **This is the GUI go/no-go gate.** |
| 5 | 12 | Dear ImGui basics | Minimal window, static hardcoded list | Window opens, shows fake data, builds cleanly on Windows |
| 5 | 13 | **why the GUI thread never touches worker data directly** | Wire manager into GUI (poll shared state each frame, read-only) | List + progress bars, live, updating during a real download |
| 6 | 14 | — | GUI buttons: add/pause/cancel wired to the manager | Can drive a full download lifecycle from the GUI without touching the console |
| 6 | 15 | — | Edge-case handling, add screenshots + GUI paragraph to the existing README, 2-3 Catch2 tests | Presentable, documented |
| 7 | 16 | — | Benchmark (single-stream vs concurrent/segmented), demo recording, final polish | Pushed, demo-able, README complete with real numbers |

## Checkpoints — what to cut, decided now

Priority order, always: **a working engine > correctness under repeated runs > the GUI > polish.**

- **Behind by end of Week 2 (session 6):** cut retry logic; reduce persistence to list-only, no status restore.
- **Behind by end of Week 3 (session 9):** cut segment-level resume (session 9) entirely; keep the one-file segmented demo from session 8.
- **Session 11 — GUI go/no-go gate:** if the segmented path is still producing intermittent failures, or you're more than a week behind, the GUI is cut and you ship console-only. This is the decision this whole schedule is built around. Do not start ImGui on top of an unstable engine.
- **Behind by end of Week 6 (session 14):** ship the GUI as a read-only view (list + progress bars, no buttons) and keep the console as the control interface. A GUI that displays correctly beats a GUI with half-working buttons.
- **Behind at Week 7:** ship whatever runs correctly, with a README honest about what's missing.

## Biggest risks

1. **STL learning under pressure** — spend session 1 specifically on `vector`/`string`/`map` before touching the project.
2. **RAII misuse (leaked handles)** — wrap curl handles and file handles in small RAII classes immediately, even before feeling fully confident.
3. **Race conditions in shared progress state** — prefer `std::atomic<size_t>` for simple counters; one mutex per job's complex state, consistent lock ordering.
4. **Deadlocks from GUI/worker interaction** — worker threads only ever write to atomics/protected data; the GUI thread only reads, never blocks on workers.
5. **Test servers not supporting Range requests** — pick 1-2 known-good large test files (e.g. public CDN-hosted files) and use them consistently. Verify `206` support before building against a URL.
6. **CMake/library linking friction (especially on Windows)** — use `vcpkg` from day 1, budget explicit setup time before "real" coding starts.
7. **GUI taking longer than expected** — Dear ImGui specifically, and strictly last in the sequence. Budget for sessions 12-14 taking 50% longer than planned; that's the single most common failure mode in this plan.
8. **File corruption from concurrent writes to one file** — write each segment to its own temp file and concatenate at the end; avoid positioned-write-to-shared-file approaches at this experience level.
9. **Orphaned temp files on cancel** — every segment's temp file needs an owner responsible for deleting it on both the success and the cancel path. RAII applies here too.
10. **Underestimating concurrency debugging time** — built-in buffer sessions (7 and 11); consider `-fsanitize=thread` if the compiler supports it, to catch races that won't be spotted by inspection.
11. **Finals collision.** Seven weeks from a late-September start puts sessions 12-14, the hardest-to-debug work in the project, in early-to-mid November, with finals a few weeks behind that. Any slip pushes GUI debugging into exam prep. If the session 11 checkpoint lands during or after the second week of November, take the cut and ship console-only — school comes first, and the console version is already a complete project.
12. **Scope creep** — the checkpoint decisions above are made now, not renegotiated under pressure later. **Calendar creep is the same risk in a new shape:** extending the weeks buys schedule slack, not license to add features back in. The 16 sessions above are the entire scope, regardless of how many weeks they take.

## Resume value

Real threading, synchronization, and networking experience is something most student projects (web apps, scripts) don't demonstrate at all. What matters most in an interview: the ability to explain the synchronization decisions (why a mutex here, an atomic there, how races were avoided), how cancellation is done cooperatively, and an understanding of Range requests. That's the real engineering signal. The GUI's contribution is that it makes the project demoable in fifteen seconds and shows you can reason about thread-safe reads from a render loop — real, but secondary to the engine underneath it. Collect one clean, real benchmark (single-stream vs. concurrent/segmented download speed on the same large file). Being upfront in the README about what was cut and why reads as engineering maturity, not weakness.

**If things go badly:** single download-to-disk + 2-3 concurrent whole-file downloads + segmentation + JSON persistence + console UI. Done well and explained clearly, that alone is already a legitimate, above-average student systems project.

## Working with the human (Olisaemeka) — pair-programming mode

- Same collaboration model as Ferguson: this is a learning project, not a delegate-and-review project. Core logic (threading/synchronization decisions, the download manager's concurrency model, segment logic) should be explained conceptually first, then attempted by him before being written for him.
- Boilerplate (RAII wrapper classes for handles, JSON struct definitions, CMake/vcpkg setup, ImGui window scaffolding) can be generated freely, but should be briefly explained — what it does and why it's structured that way.
- **Debugging is his, not Claude's.** When an attempt has a bug, Claude may suggest debugging *techniques* (isolating the failing code, printing a subset of state, narrowing by bisection) but gives no hints toward the actual cause until it's clear he has genuinely tried and exhausted his own approaches. Even then, a nudge toward the area, never the solution.
- Small commits, one feature each. Verify by actually building and running the program repeatedly (not by assuming a compile-clean state means correctness) — concurrency bugs are often silent until you run the code many times.
- Checkpoints (sessions 6, 9, 11, 14) are real decision points — at each one, honestly assess progress and apply the corresponding cut if behind, rather than pushing the original full scope further. **Session 11 is the one that matters most.**
- School comes first — a missed or short week doesn't mean the project failed, it means the next checkpoint assessment should be honest about where things actually stand.
- **Let him fail on purpose sometimes, don't pre-empt every mistake.** Reading a real compiler error, watching a race condition actually produce a wrong number, or hitting a crash from a missing null-check teaches why a safeguard exists far better than being told about it in advance. Don't warn him away from a mistake he's about to make just because it's foreseeable — let him write it, run it, see the actual error message or wrong output, and work out why from there. The line: intervene before something that would burn a large amount of time for no learning value (e.g. a genuinely obscure CMake/vcpkg linker error with no conceptual payoff) — not before an error that teaches the actual lesson this project exists to teach.
- **Enforce correct terminology, both in conversation and in the code itself.** If he says "process" meaning "thread," "parallel" meaning "concurrent," "lock" meaning something other than a mutex, or otherwise uses a term imprecisely, correct it plainly before moving on. Same standard applies to naming in the code: a variable/function name should not claim a guarantee the code doesn't actually provide (e.g. don't name something `atomic_x` if it's actually mutex-protected, don't call something a `Range` request if it isn't sending a `Range` header). Precise vocabulary is part of actually understanding the concept, not pedantry.
- **Hold the stop-anywhere boundaries from session 5, not from session 12.** If he writes console code that reads manager state directly instead of through the snapshot, or puts a decision inside what will become GUI code, say so at the time. Those two mistakes are cheap to fix in session 5 and expensive in session 13, and they are the only thing that would turn a GUI cut into a rewrite.
- **Scope is closed.** It took five reversals to settle this. If he proposes adding or removing a feature mid-project, point back to this file and the relevant checkpoint rather than re-litigating it.
