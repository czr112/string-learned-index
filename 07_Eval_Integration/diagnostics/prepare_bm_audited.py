"""Priority 2: audited trace generator.

Fixes the three defects the record review found:
  * growth draws distinct held-out keys - no duplicate inserts;
  * replacement erases a live key and inserts a held-out *corpus* key (not a synthetic suffix),
    so the key set is preserved and the 2:1:1 split is exact over successful operations;
  * the hotspot fraction is measured over *writes* and the achievable fraction is checked
    against the available held-out keys inside the hot rank range before anything is frozen.

Every trace is audited before it is written; a failing audit aborts the run.
"""
import hashlib
import json
import random
import struct
import sys
from pathlib import Path

WS = Path('/mnt/d/StringLearnedIndex_Workspace')
ROOT = WS / '07_Eval_Integration'
BENCH = ROOT / 'upstream_snapshots/benchmark_003a55bf'
sys.path.insert(0, str(BENCH / 'scripts'))
from benchmark_lib.interchange import write_operations, write_rows  # noqa: E402

DATASETS = {
    'words': '07_Eval_Integration/results/bm_real_20261018/interchange/english_words_rows.bin',
    'urls': '07_Eval_Integration/results/bm_real_20261018/interchange/memetracker_urls_rows.bin',
    'words_indep': '07_Eval_Integration/results/bm_space_gen_inputs_20261019/interchange/'
                   'english_words_s20261019_rows.bin',
}
SEED = 20261024
OPS = 20000
HOT_SHARE = 0.8
SCAN_LIMITS = (16, 128)


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()


def read_rows(path):
    data = path.read_bytes(); assert data[:8] == b'SIBROW1\0'
    count, = struct.unpack_from('<Q', data, 8); pos = 16; out = []
    for _ in range(count):
        n, = struct.unpack_from('<Q', data, pos); pos += 8
        key = data[pos:pos + n]; pos += n
        value, = struct.unpack_from('<Q', data, pos); pos += 8
        out.append((key, value))
    return out


def plan(keys, ratio, locality, semantics, seed):
    rng = random.Random(seed)
    ordered = sorted(k for k, _ in keys)
    rank = {k: i for i, k in enumerate(ordered)}
    width = max(1, len(ordered) // 10)
    start = rng.randrange(0, len(ordered) - width + 1)
    hot_lo, hot_hi = start, start + width - 1          # hot slice by source key rank

    def in_hot(key):
        return hot_lo <= rank[key] <= hot_hi

    # Initial live set and held-out pool of *real* keys.
    shuffle = list(range(len(ordered))); rng.shuffle(shuffle)
    if semantics == 'growth':
        cut = len(ordered) * 4 // 5
        live_idx = set(shuffle[:cut])
    else:
        cut = len(ordered) * 9 // 10                    # keep 10% of real keys in reserve
        live_idx = set(shuffle[:cut])
    initial = [(ordered[i], i) for i in sorted(live_idx)]
    live = set(initial_keys := [k for k, _ in initial])
    held = [ordered[i] for i in sorted(set(range(len(ordered))) - live_idx)]
    held_hot = [k for k in held if in_hot(k)]

    find_ops = int(OPS * (1 - ratio))
    write_ops = OPS - find_ops
    ops = []
    absent = [k + b'~absent' for k in ordered[:500]]
    for _ in range(find_ops):
        if rng.randrange(10) == 9:
            ops.append((1, rng.choice(absent), 0, 0))
        else:
            key = rng.choice([k for k in live]) if locality == 'hotspot' and rng.randrange(10) < 8 else rng.choice(ordered)
            ops.append((1, key, 0, 0))

    inserted = []
    if semantics == 'growth':
        # Distinct inserts only; hotspot fraction limited by the held-out keys inside the slice.
        hot_target = int(round(write_ops * HOT_SHARE)) if locality == 'hotspot' else 0
        hot_quota = min(hot_target, len(held_hot))
        pool_hot = held_hot[:hot_quota]
        pool_cold = [k for k in held if k not in set(pool_hot)]
        need = write_ops
        if len(pool_hot) + len(pool_cold) < need:
            raise SystemExit(f'insufficient held-out keys: need {need}, have {len(held)+0}')
        for i in range(need):
            key = pool_hot[i] if i < len(pool_hot) else pool_cold[i - len(pool_hot)]
            ops.append((2, key, len(ordered) + i, 0))
            inserted.append(key)
        achievable_hot = len(pool_hot) / need
    else:
        pairs = write_ops // 4
        updates = pairs * 2
        live_now = set(live)
        # Disjoint pools: an update never targets a key another operation erases, so the
        # 2:1:1 split over *successful* operations survives any interleaving after the shuffle.
        all_live = sorted(live_now)
        update_pool = [k for i, k in enumerate(all_live) if i % 2 == 0]
        erase_pool = [k for i, k in enumerate(all_live) if i % 2 == 1]
        hot_update_pool = [k for k in update_pool if in_hot(k)]
        hot_erase_pool = [k for k in erase_pool if in_hot(k)]
        hot_live = hot_update_pool
        cold_live = update_pool
        held_iter = [k for k in held_hot] + [k for k in held if k not in set(held_hot)]
        held_cursor = 0
        hot_writes = 0
        write_total = 0
        hot_updates_done = 0
        hot_target_updates = int(round(updates * HOT_SHARE)) if locality == 'hotspot' else 0
        for i in range(updates):
            if locality == 'hotspot' and hot_updates_done < hot_target_updates and hot_live:
                pool = [k for k in hot_live if k in live_now]
                key = rng.choice(pool) if pool else rng.choice(cold_live)
                if key in set(hot_live):
                    hot_updates_done += 1
                    hot_writes += 1
            else:
                key = rng.choice(cold_live)
            ops.append((3, key, len(ordered) + 10_000 + i, 0))
            write_total += 1
        used_erase = set()
        for i in range(pairs):
            available = [k for k in erase_pool if k not in used_erase]
            hot_available = [k for k in hot_erase_pool if k not in used_erase]
            use_hot = locality == 'hotspot' and hot_available and (hot_writes < HOT_SHARE * (write_total + 2 * (pairs - i)))
            erase_key = rng.choice(hot_available) if use_hot else rng.choice(available)
            used_erase.add(erase_key)
            new_key = held_iter[held_cursor % len(held_iter)]
            held_cursor += 1
            while new_key in live_now:                    # never insert a live key
                new_key = held_iter[held_cursor % len(held_iter)]
                held_cursor += 1
            ops.append((4, erase_key, 0, 0))
            live_now.discard(erase_key)
            ops.append((2, new_key, len(ordered) + 20_000 + i, 0))
            live_now.add(new_key)
            inserted.append(new_key)
            write_total += 2
            if in_hot(erase_key):
                hot_writes += 1
            if in_hot(new_key):
                hot_writes += 1
        achievable_hot = hot_writes / max(1, write_total)

    for i in range(max(1, OPS // 50)):
        ops.append((5, rng.choice(ordered), 0, SCAN_LIMITS[i % 2]))
    rng.shuffle(ops)
    return initial, ops, {'hot_range': [hot_lo, hot_hi], 'held_out': len(held),
                          'held_out_in_hot': len(held_hot), 'achievable_hot': round(achievable_hot, 4)}


def audit(initial, ops, semantics, locality, target_hot):
    initial_keys = [k for k, _ in initial]
    live = set(initial_keys)
    counts = {'find': 0, 'insert': 0, 'update': 0, 'erase': 0, 'scan': 0}
    ok = {'insert': 0, 'update': 0, 'erase': 0}
    dup = 0
    writes_hot = 0
    writes = 0
    for kind, key, _value, _limit in ops:
        name = {1: 'find', 2: 'insert', 3: 'update', 4: 'erase', 5: 'scan'}[kind]
        counts[name] += 1
        if kind == 2:
            if key in live:
                dup += 1
            else:
                live.add(key); ok['insert'] += 1
            writes += 1
        elif kind == 3:
            if key in live:
                ok['update'] += 1
            writes += 1
        elif kind == 4:
            if key in live:
                live.discard(key); ok['erase'] += 1
            writes += 1
    report = {'requested': counts, 'successful': ok, 'duplicate_inserts': dup,
              'live_after': len(live), 'live_before': len(initial_keys),
              'net_growth': len(live) - len(initial_keys),
              'kind_sum': sum(counts.values()), 'ops': len(ops)}
    problems = []
    if report['kind_sum'] != len(ops):
        problems.append('kind accounting mismatch')
    if semantics == 'growth':
        if dup:
            problems.append(f'growth has {dup} duplicate inserts')
        if ok['insert'] != counts['insert']:
            problems.append('growth insert success mismatch')
    else:
        if not (ok['update'] == 2 * ok['erase'] and ok['erase'] == ok['insert']):
            problems.append(f"replacement split is {ok['update']}:{ok['erase']}:{ok['insert']}, want 2:1:1")
        if len(live) != len(initial_keys):
            problems.append('replacement did not preserve the key count')
    report['problems'] = problems
    return report


def main():
    out = ROOT / 'results' / 'bm_audited_traces_20261024'
    out.mkdir(exist_ok=False)
    traces = out / 'traces'; traces.mkdir()
    manifest = {}
    for name, rel in DATASETS.items():
        keys = read_rows(WS / rel)
        for ratio_name, ratio in (('95_5', 0.05), ('80_20', 0.20)):
            for locality in ('uniform', 'hotspot'):
                for semantics in ('growth', 'replacement'):
                    cell = f'{name}_{ratio_name}_{locality}_{semantics}'
                    initial, ops, meta = plan(keys, ratio, locality, semantics, SEED + len(cell))
                    report = audit(initial, ops, semantics, locality, meta['achievable_hot'])
                    if report['problems']:
                        raise SystemExit(f'{cell}: audit failed: {report["problems"]}')
                    rows_path = traces / f'{cell}_rows.bin'
                    rowhash = write_rows(rows_path, [k for k, _ in initial], [v for _, v in initial])
                    op_path = traces / f'{cell}_ops.bin'
                    ophash = write_operations(op_path, ops)
                    manifest[cell] = {**meta, 'audit': report, 'dataset': name, 'ratio': ratio_name,
                                      'locality': locality, 'semantics': semantics,
                                      'rows_path': str(rows_path.relative_to(WS)), 'rows_sha256': rowhash,
                                      'operations_path': str(op_path.relative_to(WS)),
                                      'operations_sha256': ophash,
                                      'hot_share_target': HOT_SHARE if locality == 'hotspot' else 0.0}
                    print(f"{cell:46s} ok={report['successful']} dup={report['duplicate_inserts']} "
                          f"net={report['net_growth']} hot_ach={meta['achievable_hot']}", flush=True)
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print('traces', len(manifest))


if __name__ == '__main__':
    main()
