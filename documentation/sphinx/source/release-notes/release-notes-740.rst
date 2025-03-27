#############
Release Notes
#############

7.4.0
=====

Features (Supported)
-----------------------

* Added support to restore from new backup's partitioned log files. (PR #11901) <https://github.com/apple/foundationdb/pull/11901>.
* Added LRU-like cache replacement for in-memory page checksums to save memory usage. (PR #11194) <https://github.com/apple/foundationdb/pull/11194>, (PR #11273) <https://github.com/apple/foundationdb/pull/11273>, and (PR #11276) <https://github.com/apple/foundationdb/pull/11276>.


Features (Experimental)
-----------------------

Performance
-----------

Reliability
-----------

Fixes
-----
* fdbmonitor: track parent process death for FreeBSD. (PR #11361) <https://github.com/apple/foundationdb/pull/11361>.
* Fixed issues where backup workers may miss mutations and assertion failures. (PR #11908) <https://github.com/apple/foundationdb/pull/11908>, (PR #12026) <https://github.com/apple/foundationdb/pull/12026>, and (PR #12046) <https://github.com/apple/foundationdb/pull/12046>.


Status
------

Bindings
--------

* Go: simplify network start check logic to address the SIGSEGV happening when network routine is started multiple times concurrently.  (PR #11104) <https://github.com/apple/foundationdb/pull/11104>.

Other Changes
-------------

* Removed upgrade support from 6.2 and earlier TLogs and make xxhash checksum the default for TLog.   (PR #11667) <https://github.com/apple/foundationdb/pull/11667>.

Dependencies
------------

* Upgraded boost to version 1.86. (PR #11788) <https://github.com/apple/foundationdb/pull/11788>.
* Upgraded awssdk to version 1.11.473. (PR #11853) <https://github.com/apple/foundationdb/pull/11853>.
* GCC 13 and Clang 19 are supported compilers.


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
