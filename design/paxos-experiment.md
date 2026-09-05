# Single-Decree Paxos coordinator experiment

`SingleDecreePaxos.h/.cpp` provides prepare/promise and accept/accepted
messages, pure acceptor transitions, and quorum proposer operations. A caller
must use unique ballots and adopt the highest accepted value returned by its
prepare quorum. A ballot must never propose two values for the same instance.
Persistence adapters must commit a changed acceptor state before replying.

`PaxosSequence.h/.cpp` builds mutable coordinated state from independent
single-decree instances numbered from 1. Reading completes any accepted
historical values, then prepares the first empty instance. Setting proposes
one value in that reserved instance. Contenders either advance the sequence
or preempt an older reservation. Use a fresh proposer UID for every read.

`CoordinatedState.cpp` preserves the public read/setExclusive/onConflict API.
`coordinator/Coordination.cpp` stores promises and accepted values in the
existing OnDemandStore, under a separate Paxos key prefix. Initial writes
require all coordinators; recovery of existing instances requires a majority.
Leader nomination and heartbeat code are unchanged; authoritative cluster
recovery uses the new coordinated-state implementation.

This branch is for fresh experimental clusters running the same binary.
It reuses the generation-register RPC endpoints with new message types and
does not migrate existing coordinated state or support mixed versions.
Do not run it against existing cluster data. It retains all instances and
scans history on reads; the persistent record contains the entire sequence.
There is no compaction or Multi-Paxos leader optimization.

Validation includes pure acceptor unit tests, a disk-store close/reopen test,
and `tests/rare/PaxosCoordinatorFaults.toml`, which combines competing
proposers with RandomClogging and rebooting Attrition. The workload checks
consecutive instance numbers and progress beyond initialization. Simulation
also retains learned chosen values and asserts that a different value is
never learned for the same key and instance. This observer detects decisions
reported by the quorum proposer; it is not an exhaustive observer of quorums
whose replies were lost. Simulation runs are evidence, not a safety proof.

Build `fdbserver_core_test`, `fdbserver_coordinator_test`, and `fdbserver`.
Run unit filters `/fdbserver/core/SingleDecreePaxos` and
`/fdbserver/Coordination/localPaxosAcceptor`. Run the fault TOML with
`fdbserver -r simulation -f <absolute-test-path> -s <seed> -b on` (or `off`).
Preserve traces and check both completion and severity-40 events.
