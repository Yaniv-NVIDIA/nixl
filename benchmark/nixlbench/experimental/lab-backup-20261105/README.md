# Preserved experimental NIXLBench harness, 2026-11-05

This branch backs up the tested lab implementation before any port to PR #4.
It is based on f6983a0911519ad3cc9b22f35e3946f929c7d743 and the previously
committed experimental baseline 93f4bf08e97cc1b31d982ed6cbf7f9c8ced12780.
It is NOT based on PR #4 head 645e2e0363be5fff6f97087313e936c461f3ec86.

The tracked benchmark files match the archived source used for the four
508/1020 KiB padding-corrected measurements in jobs 118836/118857. The other
14 sweep measurements used the preceding archive; only main.cpp changed
between those two archives. source-change-manifest.json records that change.
source-sha256.json identifies the corrected archive and all restored tracked files.
Vendored dependency downloads and build outputs are excluded from this branch.

Preserved features include the expanded remote key pool, the restored full-width
random key helper, per-lane local buffer reuse, logical buffer wrap excluding
hugepage padding, duration-based measurement, worker start gates, and native
completion/in-flight evidence. These experimental controls have not been ported
onto the PR implementation and are not presented as submission-ready code.

The lab measurement runs for a configured duration (30 seconds in the final
sweep), stops issuing, drains outstanding transfers, and derives throughput from
completed operations and measured elapsed time. PR #4 instead terminates after
a configured iteration count. Synchronization gates are useful for multi-worker
or multi-process overlap and unnecessary for one worker.

The descriptor fixture covers the actual descriptor-generation method, including
508/1020 KiB logical wrap and aligned-size mappings. It is retained as evidence;
this backup operation does not build or rerun benchmarks. Production libraries
were not changed. Historical source archive and result evidence remain under
Downloads/nvbug-6788210/nixl-full-sweep-b16d2-b32d2-daria05-20261105.
