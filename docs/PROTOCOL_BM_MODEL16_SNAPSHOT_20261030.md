# Model16 snapshot factory acceptance

Prerequisite: fixed16 work diagnosis at7ca8b7b nominates snapshot only. Dynamic
fails its directory-work guard and remains closed. Keep upstream model3894c767,
frozen algorithm10e0494 and the exact generator in run_bm_model16_work.py. No new
budget, routing, encoding, block occupancy or structural intervention.

## Explicit surface

Add make_bm_model16_snapshot_index, outside production factories/defaults. It uses
the same generated m16 TX Index as the nominated snapshot, only bulk_load/find/
owning scan. Insert/update/erase/range_sum must declare unsupported and throw
UnsupportedOperation without mutation, including before bulk_load. Reject every
parameter. Record real closure SHA, adapter SHA, model commit, segment_budget16,
and read-only scope. Never label dynamic acceptance or general model adoption.

bulk_load publishes only after complete construction; on allocation failure the
same fresh adapter must remain unbuilt and accept a retry. This extra retry
property is an experiment acceptance requirement, beyond the one-call contract.
No input is retained. Expose only existing verify() diagnostic seam on Adapter.

## Tests before timing

- Same borrowed_contract_acceptance.cpp via actual factory, opt and nonrecovering
  ASan/UBSan/LSan; report actual check count for the read-only capability set.
- All32 fixed terminal snapshots and historical query groups, canonical five
  per-cell hashes, exact find values and full ordered owning scan, verify(),
  zero allocations in factory find over every historical probe. Use public Case
  input/group validation and preserve original TX8 control identity.
- Actual requested-byte ledger includes adapter and index, excludes pre-existing
  input/oracle and later diagnostics; measure terminal/build peak before scan
  verification. TX8 snapshot is direct factory control. Each of32 terminal and
  build-peak ratios must be<=1.05. No maintenance-peak claim for read-only build.
- Fault fixtures: deterministic binary keys at cardinalities0,15,16,17,2048,2049;
  first2049 rows of each fixed english_words/memetracker_urls/memetracker_phrases
  corpus. Preload fixtures before accounting. Do not select fixtures from results.
- Enumerate every actual bulk_load allocation ordinal in each fixture, plus one
  n+1 positive control. After each bad_alloc require exact pre-operation live
  bytes, unbuilt state and successful retry; verify all rows and1000 zero-alloc
  reads, then destroy to zero. Independently enumerate all full-scan allocation
  ordinals with n+1 control: failure preserves live bytes/rows, retry is exact.
  Use opt and sanitizer with the same semantic/fault counts.
- Check read-only rejection preserves exact state on existing/missing keys;
  complete source/input/binary drift checks. No concurrent compiler/timer jobs.

## Decision

Any correctness, leak, identity or space failure stops qualification for diagnosis;
do not time an unsafe factory. Passing means only read-only functional/space
acceptance. No speedup, no dynamic pass, no scale expansion and no default change.
Context16 remains default. Existing WSL and native timing qualification failures
remain recorded; a later latency trial requires its own environment/protocol.

Use shared Run atomic/hash/closure discipline and frozen16 generator. Preserve
commands, generated sources, raw failures, all per-cell records and per-ordinal
fault evidence. Prior work diagnosis must finish before this run starts.
