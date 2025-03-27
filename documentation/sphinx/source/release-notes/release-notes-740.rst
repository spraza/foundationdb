#############
Release Notes
#############

7.4.0
=====

Features (Supported)
-----------------------

* Added support to restore from new backup's partitioned log files. `(PR #11901) <https://github.com/apple/foundationdb/pull/11901>`_
* Added LRU-like cache replacement for in-memory page checksums to save memory usage. `(PR #11194) <https://github.com/apple/foundationdb/pull/11194>`_, `(PR #11273) <https://github.com/apple/foundationdb/pull/11273>`_, and `(PR #11276) <https://github.com/apple/foundationdb/pull/11276>`_

#11717 [fdbserver] Gray failure and simulator improvements related to remote processes spraza:gray-failure-features-PR about 5 months ago
#11753 Gray failure allows storage servers to complain spraza:gray-failure-allow-ss about 4 months ago. Also include with item above.

https://github.com/apple/foundationdb/pull/9984 (Add networkoption to disable non-TLS connections)

Features (Experimental)
-----------------------

Bulk loading: Fast load TBs' snapshot of key-values from S3 to an empty cluster.
Doc: https://github.com/apple/foundationdb/blob/main/documentation/sphinx/source/bulkload-user.rst
PR: https://github.com/apple/foundationdb/pull/11369
PR: https://github.com/apple/foundationdb/pull/12036
PR: https://github.com/apple/foundationdb/pull/11952

Bulk dumping: Fast dump TBs' snapshot of key-values to S3 from an idle cluster.
Doc: https://github.com/apple/foundationdb/blob/main/documentation/sphinx/source/bulkdump.rst
PR: https://github.com/apple/foundationdb/pull/11822
PR: https://github.com/apple/foundationdb/pull/11780

Exclusive read range lock: Block user write traffic to a specific range.
Doc: https://github.com/apple/foundationdb/blob/main/documentation/sphinx/source/rangelock.rst
PR: https://github.com/apple/foundationdb/pull/11693
PR: https://github.com/apple/foundationdb/pull/11986
PR: https://github.com/apple/foundationdb/pull/12047

Mutation checksum/Accumulative checksum: Conduct real-time detection of mutation corruptions on write path.
PR: https://github.com/apple/foundationdb/pull/11255
PR: https://github.com/apple/foundationdb/pull/11751
PR: https://github.com/apple/foundationdb/pull/11319

Detect hot shards and throttle commits to them
10970 2023-10-31T16:01:35Z Throttle commits against hot shards. <NOTE: 10970 is github PR number, remove date. Also note that other items in this file may have the same pattern, do the same thing for them>.

Synthesize test data on a cluster
11115 2024-01-08T17:56:41Z Fix bug in synthetic data creation
11107 2024-01-04T17:17:40Z Add support to synthesize data in QA clusters


Version vector - send commmits only to logs buddied with SS that will receive mutations <No PR for this one for now>

#11899 Enable SS upload/download to S3 (for BulkLoad) (11899 is the github pr)
#11988 Add checksumming across multipart upload and download. Part of bulk load item above.
#11920 Add multiparting to s3client. Part of bulk load item above.

#11235 Compare storage replicas on reads (Note: This PR introduced the feature. There are a bunch of PRs merged on top of this, not sure if we need to include all those PRs here.)

[Experimental] gRPC integration with Flow #11782 #12023 #12004 #12005 #11892, #11984

Performance
-----------

https://github.com/apple/foundationdb/pull/11435 (Remove two ptree searches when processing a clear)
https://github.com/apple/foundationdb/pull/10878 (Add yields to backup agents to avoid slow tasks)
    * https://github.com/apple/foundationdb/pull/10354/files (Improve performance of TransactionTagCounter)
    * https://github.com/apple/foundationdb/pull/10662 (Fix GRV queue leak)
    * https://github.com/apple/foundationdb/pull/10725 (Monitor multiple write tags in StorageQueueInfo::refreshCommitCost)
    * https://github.com/apple/foundationdb/pull/10810 (Fix quota throttler clear cost estimation)


Reliability
-----------

Fixes
-----
* fdbmonitor: tracked parent process death for FreeBSD. `(PR #11361) <https://github.com/apple/foundationdb/pull/11361>`_
* Fixed issues where backup workers missed mutations and caused assertion failures. `(PR #11908) <https://github.com/apple/foundationdb/pull/11908>`_, `(PR #12026) <https://github.com/apple/foundationdb/pull/12026>`_, and `(PR #12046) <https://github.com/apple/foundationdb/pull/12046>`_.
* Fixed AuditStorage empty range read error. `(PR #12043) <https://github.com/apple/foundationdb/pull/12043>`_
* Built a sidecar container that refreshed S3 credentials. `(PR #11945) <https://github.com/apple/foundationdb/pull/11945>`_
* Prevented failover when storage servers were behind. `(PR #11054) <https://github.com/apple/foundationdb/pull/11054>`_
* Fixed FdbServer not being able to join the cluster. `(PR #9814) <https://github.com/apple/foundationdb/pull/9814>`_
* Fixed wrong implementation of isOnMainThread in Simulation and Testing. `(PR #11978) <https://github.com/apple/foundationdb/pull/11978>`_
* Called IThreadReceiver::init() in DummyThreadPool for proper initialization. `(PR #11718) <https://github.com/apple/foundationdb/pull/11718>`_
* Added LOG_CONNECTION_ATTEMPTS_ENABLED and CONNECTION_LOG_DIRECTORY to log all incoming connections to an external file. `(PR #11704) <https://github.com/apple/foundationdb/pull/11704>`_
* Fixed check in getExactRange that determines whether we can return early. `(PR #10522) <https://github.com/apple/foundationdb/pull/10522>`_
* Let coordination server crash on file_not_found error. `(PR #10363) <https://github.com/apple/foundationdb/pull/10363>`_
* Fixed computeRestoreEndVersion bug when outLogs is null. `(PR #10488) <https://github.com/apple/foundationdb/pull/10488>`_
* Initialized apply mutations map for restore to version. `(PR #10857) <https://github.com/apple/foundationdb/pull/10857>`_
* Fixed stuck watch bug. `(PR #11112) <https://github.com/apple/foundationdb/pull/11112>`_
* Reset connection idle time when restarting connection monitor. `(PR #10495) <https://github.com/apple/foundationdb/pull/10495>`_
* Fixed tss kill logic: only disabled Tss check when zeroHealthyTeams=false. `(PR #10711) <https://github.com/apple/foundationdb/pull/10711>`_

Status
------
* Added RocksDB version to status JSON. `(PR #11868) <https://github.com/apple/foundationdb/pull/11868>`_
* Added support to fetch a specific group of status JSON fields. `(PR #10927) <https://github.com/apple/foundationdb/pull/10927>`_
* Prevented Status actor from bubbling up timeout error. `(PR #10791) <https://github.com/apple/foundationdb/pull/10791>`_

Bindings
--------
* Simplified network start check logic to address SIGSEGV happening when network routine was started multiple times concurrently in Go bindings. `(PR #11104) <https://github.com/apple/foundationdb/pull/11104>`_
* Fixed Go binding: Do not automatically close database objects. `(PR #11394) <https://github.com/apple/foundationdb/pull/11394>`_
* Fixed bug with R/O transaction destroyed before futures in Go binding. `(PR #11611) <https://github.com/apple/foundationdb/pull/11611>`_
* Allowed cancelling snapshots and R/O transactions in Go binding. `(PR #11614) <https://github.com/apple/foundationdb/pull/11614>`_
* Added GetClientStatus method to Database in Go binding. `(PR #11627) <https://github.com/apple/foundationdb/pull/11627>`_
* Do not override wrapped transaction error in Go binding. `(PR #11810) <https://github.com/apple/foundationdb/pull/11810>`_
* Fixed panic when connecting to database from multiple threads in Go bindings. `(PR #10702) <https://github.com/apple/foundationdb/pull/10702>`_

Other Changes
-------------
* Removed upgrade support from 6.2 and earlier TLogs and made xxhash checksum the default for TLog. `(PR #11667) <https://github.com/apple/foundationdb/pull/11667>`_
* Added rate keeper logs for zones with lowest tps. `(PR #11067) <https://github.com/apple/foundationdb/pull/11067>`_
* Documentation says backup blob URL can optionally contain key/secret/token. `(PR #11825) <https://github.com/apple/foundationdb/pull/11825>`_
* Added exclude in progress signal to fdbcli. `(PR #11569) <https://github.com/apple/foundationdb/pull/11569>`_
* Ensured storage and tlog are always set to a valid type. `(PR #10876) <https://github.com/apple/foundationdb/pull/10876>`_
* Enabled MovingData to show overall moved bytes rather than just one copy. `(PR #10076) <https://github.com/apple/foundationdb/pull/10076>`_

Dependencies
------------
* Upgraded boost to version 1.86. `(PR #11788) <https://github.com/apple/foundationdb/pull/11788>`_
* Upgraded awssdk to version 1.11.473. `(PR #11853) <https://github.com/apple/foundationdb/pull/11853>`_
* Supported GCC 13 and Clang 19 compilers.
* Upgraded RocksDB to 9.7.3. `(PR #11735) <https://github.com/apple/foundationdb/pull/11735>`_


Earlier release notes
---------------------
* :doc:`7.3 (API Version 730) </release-notes/release-notes-730>`
* :doc:`7.2 (API Version 720) </release-notes/release-notes-720>`
* :doc:`7.1 (API Version 710) </release-notes/release-notes-710>`
* :doc:`7.0 (API Version 700) </release-notes/release-notes-700>`
* :doc:`6.3 (API Version 630) </release-notes/release-notes-630>`
* :doc:`6.2 (API Version 620) </release-notes/release-notes-620>`
* :doc:`6.1 (API Version 610) </release-notes/release-notes-610>`
* :doc:`6.0 (API Version 600) </release-notes/release-notes-600>`
* :doc:`5.2 (API Version 520) </release-notes/release-notes-520>`
* :doc:`5.1 (API Version 510) </release-notes/release-notes-510>`
* :doc:`5.0 (API Version 500) </release-notes/release-notes-500>`
* :doc:`4.6 (API Version 460) </release-notes/release-notes-460>`
* :doc:`4.5 (API Version 450) </release-notes/release-notes-450>`
* :doc:`4.4 (API Version 440) </release-notes/release-notes-440>`
* :doc:`4.3 (API Version 430) </release-notes/release-notes-430>`
* :doc:`4.2 (API Version 420) </release-notes/release-notes-420>`
* :doc:`4.1 (API Version 410) </release-notes/release-notes-410>`
* :doc:`4.0 (API Version 400) </release-notes/release-notes-400>`
* :doc:`3.0 (API Version 300) </release-notes/release-notes-300>`
* :doc:`2.0 (API Version 200) </release-notes/release-notes-200>`
* :doc:`1.0 (API Version 100) </release-notes/release-notes-100>`
* :doc:`Beta 3 (API Version 23) </release-notes/release-notes-023>`
* :doc:`Beta 2 (API Version 22) </release-notes/release-notes-022>`
* :doc:`Beta 1 (API Version 21) </release-notes/release-notes-021>`
* :doc:`Alpha 6 (API Version 16) </release-notes/release-notes-016>`
* :doc:`Alpha 5 (API Version 14) </release-notes/release-notes-014>`
