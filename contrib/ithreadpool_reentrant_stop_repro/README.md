# ThreadPool reentrant stop() repro

Reproduces a crash where a `ThreadPool` (`flow/IThreadPool.cpp`) worker
thread can end up calling `ThreadPool::stop()` on itself, from inside its
own `run()`, and trip `~Thread()`'s `ASSERT_ABORT(!userObject)`:

```
Assertion !userObject failed @ flow/IThreadPool.cpp <line>:
SIGNAL: Aborted (6)
```

## The bug in one paragraph

`fdbcli> suspend <seconds> <address>` sends a `RebootRequest`. The handler
(`fdbserver/worker.actor.cpp`) calls `g_network->stop()` before sleeping.
`Net2::onMainThread()` (`flow/Net2.cpp` on `main`, `flow/Net2.actor.cpp` on
release branches) silently drops any hand-off request once the network is
stopped, instead of consuming it - so if an `IThreadPool` worker thread
(e.g. a RocksDB read completing via `ThreadReturnPromise::send()`) hands off
to the main thread at exactly that moment, the abandoned promise fires a
`broken_promise()` *inline on that worker thread* instead of the main
thread. That can cascade through `StorageServer`'s error handling into
`IKeyValueStore::close()` -> `ThreadPool::stop()`, now running reentrantly
from inside one of that pool's own worker threads - which can't safely
join or delete itself, since it hasn't returned from `run()` yet.

This is easy to conflate with the (different, already-fixed-on-`main`)
double-free in `ThreadPool::stop()`/`delref()` fixed by commit `98241ec2ba`
("Fix double free error in ThreadPool (#12473)") - both trip the exact same
assertion. This repro reproduces the *reentrancy* bug specifically; it is
present on `main` and every current release branch as of this writing.

## What this script does

Builds a small, real (non-simulated) 2-region HA cluster - a `double`
redundancy `ssd-rocksdb-v1` cluster with 3 coordinators + 2 storage
processes in a `primary` region, and 2 storage processes in a `remote`
region - because a single-process or single-region cluster does not
reliably trigger the race (there is no remote-region read/write traffic
concurrent with the suspend). It then runs concurrent read/write load
against the cluster while repeatedly calling `suspend` on the remote
storage processes, watching for a nonzero exit.

Empirically, an unfixed checkout crashes within the first ~100 suspend
cycles under load; a fixed checkout survives 1000+ cleanly.

## Usage

```
FDBSERVER_BIN=/path/to/fdbserver FDBCLI_BIN=/path/to/fdbcli ./repro.sh
```

Both `FDBSERVER_BIN` and `FDBCLI_BIN` default to `fdbserver`/`fdbcli` on
`PATH` if not set. Recommended: build with `-DUSE_ASAN=ON` so a crash is
unambiguous and immediately actionable (a stack trace pointing at
`~Thread()`/`ThreadPool::stop()`), though a plain build will also abort
via the assertion.

Other environment variables (all optional):

| Variable         | Default                          | Meaning                                    |
|-------------------|-----------------------------------|---------------------------------------------|
| `WORKDIR`         | a fresh `mktemp -d`               | where cluster data/logs/state are written   |
| `ITERATIONS`       | `1000`                            | number of suspend cycles to attempt         |
| `SUSPEND_WAIT`     | `1`                                | seconds passed to `suspend <seconds> ...`   |
| `BASE_PORT`        | `4500`                            | first port used; the script uses 9 ports starting here, skipping ahead for the "remote" group (see `repro.sh` for the exact layout) |

Exit code is `0` if every suspend cycle completed cleanly, `1` if a
crash was detected (with the crashed process's stderr and trace file
printed).

## Cleanup

The script kills everything it started (via a `trap ... EXIT`) when it
exits, whether that's a clean finish, a detected crash, or `Ctrl-C`.
`$WORKDIR` is left behind for inspection either way - remove it yourself
once you're done.
