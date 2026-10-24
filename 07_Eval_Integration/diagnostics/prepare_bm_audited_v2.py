"""S1: audited traces with uniform sampling, matched hit rates and post-hoc hotspot counting.

Fixes the three distribution defects:
  * growth inserts are drawn uniformly without replacement from the whole held-out pool, not
    from the front of it;
  * finds target the live set identically in both localities (hotspot only moves *writes*), so
    the cold/hot comparison no longer mixes hit-rate differences;
  * the hotspot fraction is recomputed by replaying the finished trace, not estimated while
    generating, and the achievable fraction is asserted against what the pool can support.
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
SEED = 20261025
OPS = 20000
HOT_SHARE = 0.8
SCAN_LIMITS = (16, 128)


def read_rows(path):
    data = path.read_bytes(); assert data[:8] == b'SIBROW1\0'
    count, = struct.unpack_from('<Q', data, 8); pos = 16; out = []
    for _ in range(count):
        n, = struct.unpack_from('<Q', data, pos); pos += 8
        key = data[pos:pos + n]; pos += n
        value, = struct.unpack_from('<Q', data, pos); pos += 8
        out.append((key, value))
    return out


def build(keys, ratio, locality, semantics, seed):
    rng = random.Random(seed)
    ordered = sorted(k for k, _ in keys)
    rank = {k: i for i, k in enumerate(ordered)}
    width = max(1, len(ordered) // 10)
    start = rng.randrange(0, len(ordered) - width + 1)
    lo, hi = start, start + width - 1

    def in_hot(key): return lo <= rank[key] <= hi

    # Reserve more keys for hotspot growth so the hot slice can actually supply 80% of inserts.
    # The reserve is chosen by semantics only, so the uniform and hotspot cells of a family
    # share the exact same initial map (the pairing requirement). Growth needs a larger reserve
    # so the hot slice can still supply 80% of its writes.
    keep_fraction = 1 / 2 if semantics == 'growth' else 3 / 4
    order = list(range(len(ordered))); rng.shuffle(order)
    live_idx = set(order[:int(len(ordered) * keep_fraction)])
    initial = [(ordered[i], i) for i in sorted(live_idx)]
    live = {k for k, _ in initial}
    # Uniform over the whole held-out pool: shuffle before use, never take a prefix.
    held = [ordered[i] for i in sorted(set(range(len(ordered))) - live_idx)]
    rng.shuffle(held)
    held_hot = [k for k in held if in_hot(k)]

    find_ops = int(OPS * (1 - ratio))
    write_ops = OPS - find_ops
    absent = [k + b'~absent' for k in ordered[:500]]
    ops = []
    # Finds: identical policy in both localities - uniform over live keys, 10% absent probes.
    live_find = sorted(live)
    for _ in range(find_ops):
        if rng.randrange(10) == 9:
            ops.append((1, rng.choice(absent), 0, 0))
        else:
            ops.append((1, rng.choice(live_find), 0, 0))

    inserted = []
    if semantics == 'growth':
        need = write_ops
        hot_quota = int(round(need * HOT_SHARE)) if locality == 'hotspot' else 0
        if hot_quota > len(held_hot):
            raise SystemExit(f'{locality}: hot pool {len(held_hot)} < quota {hot_quota}')
        picks = held_hot[:hot_quota] + [k for k in held if k not in set(held_hot[:hot_quota])]
        if len(picks) < need:
            raise SystemExit(f'insufficient held-out keys: need {need}, have {len(picks)}')
        for i in range(need):
            key = picks[i]
            ops.append((2, key, len(ordered) + i, 0))
            inserted.append(key)
    else:
        pairs = write_ops // 4
        updates = pairs * 2
        pool = sorted(live)
        hot_pool = [k for k in pool if in_hot(k)]
        cold_pool = [k for k in pool if not in_hot(k)]
        # Disjoint update/erase pools keep 2:1:1 exact under any interleaving.
        upd_pool = pool[0::2]
        era_pool = pool[1::2]
        hot_upd = [k for k in upd_pool if in_hot(k)]
        cold_upd = [k for k in upd_pool if not in_hot(k)]
        hot_era = [k for k in era_pool if in_hot(k)]
        cold_era = [k for k in era_pool if not in_hot(k)]
        already = set()
        hot_upd_quota = int(round(updates * HOT_SHARE)) if locality == 'hotspot' else 0
        for i in range(updates):
            use_hot = locality == 'hotspot' and i < hot_upd_quota and hot_upd
            key = rng.choice(hot_upd) if use_hot else rng.choice(upd_pool)
            ops.append((3, key, len(ordered) + 10_000 + i, 0))
        # Hotspot replacement must also insert inside the slice, otherwise the achievable
        # ratio caps at 3/4 by construction.
        hot_pair_quota = int(round(pairs * HOT_SHARE)) if locality == 'hotspot' else 0
        if locality == 'hotspot':
            held_rest = [k for k in held_hot if k not in live]
        else:
            held_rest = [k for k in held if k not in live]
        used_new = set()
        cursor = 0
        for i in range(pairs):
            avail_hot = [k for k in hot_era if k not in already]
            avail_cold = [k for k in cold_era if k not in already]
            use_hot = locality == 'hotspot' and i < hot_pair_quota and avail_hot
            key = rng.choice(avail_hot) if use_hot else rng.choice([k for k in era_pool if k not in already])
            already.add(key)
            while cursor < len(held_rest) and held_rest[cursor] in used_new:
                cursor += 1
            if cursor >= len(held_rest):
                raise SystemExit('replacement ran out of held-out keys')
            new_key = held_rest[cursor]; used_new.add(new_key); cursor += 1
            ops.append((4, key, 0, 0))
            ops.append((2, new_key, len(ordered) + 20_000 + i, 0))
            inserted.append(new_key)

    for i in range(max(1, OPS // 50)):
        ops.append((5, rng.choice(ordered), 0, SCAN_LIMITS[i % 2]))
    rng.shuffle(ops)
    return initial, ops, {'hot_range': [lo, hi], 'held_out': len(held),
                          'held_out_in_hot': len(held_hot), 'keep_fraction': round(keep_fraction, 3)}


def replay_audit(initial, ops, hot_range, rank):
    live = {k for k, _ in initial}
    kinds = {'find': 0, 'insert': 0, 'update': 0, 'erase': 0, 'scan': 0}
    ok = {'insert': 0, 'update': 0, 'erase': 0, 'find_hit': 0}
    dup = 0
    writes = 0
    writes_hot = 0
    lo, hi = hot_range

    def in_hot(key):
        r = rank.get(key)
        return r is not None and lo <= r <= hi

    for kind, key, _v, _l in ops:
        name = {1: 'find', 2: 'insert', 3: 'update', 4: 'erase', 5: 'scan'}[kind]
        kinds[name] += 1
        if kind == 1:
            if key in live: ok['find_hit'] += 1
        elif kind == 2:
            if key in live:
                dup += 1
            else:
                live.add(key); ok['insert'] += 1; writes += 1
                if in_hot(key): writes_hot += 1
        elif kind == 3:
            if key in live: ok['update'] += 1
            writes += 1
            if in_hot(key): writes_hot += 1
        elif kind == 4:
            if key in live:
                live.discard(key); ok['erase'] += 1
            writes += 1
            if in_hot(key): writes_hot += 1
    return {'kinds': kinds, 'successful': ok, 'duplicate_inserts': dup,
            'live_after': len(live), 'live_before': len(initial),
            'net_growth': len(live) - len(initial),
            'writes': writes, 'writes_in_hot': writes_hot,
            'hot_write_fraction_measured': round(writes_hot / writes, 4) if writes else 0.0,
            'find_hit_rate_measured': round(ok['find_hit'] / kinds['find'], 4) if kinds['find'] else 0.0}


def main():
    out = ROOT / 'results' / 'bm_audited_v3_20261025'
    out.mkdir(exist_ok=False)
    traces = out / 'traces'; traces.mkdir()
    manifest = {}
    hit_by_ratio = {}
    for name, rel in DATASETS.items():
        keys = read_rows(WS / rel)
        ordered = sorted(k for k, _ in keys)
        rank = {k: i for i, k in enumerate(ordered)}
        for ratio_name, ratio in (('95_5', 0.05), ('80_20', 0.20)):
            for locality in ('uniform', 'hotspot'):
                for semantics in ('growth', 'replacement'):
                    cell = f'{name}_{ratio_name}_{locality}_{semantics}'
                    initial, ops, meta = build(keys, ratio, locality, semantics, SEED + len(cell))
                    audit = replay_audit(initial, ops, meta['hot_range'], rank)
                    problems = []
                    if audit['duplicate_inserts']:
                        problems.append('duplicate inserts')
                    if semantics == 'replacement':
                        s = audit['successful']
                        if not (s['update'] == 2 * s['erase'] and s['erase'] == s['insert']):
                            problems.append(f"split {s['update']}:{s['erase']}:{s['insert']}")
                        if audit['net_growth'] != 0:
                            problems.append('key count not preserved')
                    if locality == 'hotspot' and audit['hot_write_fraction_measured'] < HOT_SHARE - 0.03:
                        problems.append(f"hotspot {audit['hot_write_fraction_measured']}")
                    if problems:
                        raise SystemExit(f'{cell}: {problems}')
                    hit_by_ratio.setdefault(ratio_name, {})[f'{locality}_{semantics}'] = \
                        audit['find_hit_rate_measured']
                    rows_path = traces / f'{cell}_rows.bin'
                    rowhash = write_rows(rows_path, [k for k, _ in initial], [v for _, v in initial])
                    op_path = traces / f'{cell}_ops.bin'
                    ophash = write_operations(op_path, ops)
                    manifest[cell] = {**meta, **audit, 'dataset': name, 'ratio': ratio_name,
                                      'locality': locality, 'semantics': semantics,
                                      'rows_path': str(rows_path.relative_to(WS)),
                                      'rows_sha256': rowhash,
                                      'operations_path': str(op_path.relative_to(WS)),
                                      'operations_sha256': ophash}
                    print(f"{cell:46s} ins={audit['successful']['insert']:5d} "
                          f"upd={audit['successful']['update']:5d} era={audit['successful']['erase']:5d} "
                          f"dup={audit['duplicate_inserts']} hit={audit['find_hit_rate_measured']:.3f} "
                          f"hot={audit['hot_write_fraction_measured']:.3f}", flush=True)
    # Hit rates must match between localities within the same ratio and semantics.
    for ratio_name, groups in hit_by_ratio.items():
        for semantics in ('growth', 'replacement'):
            u = groups.get(f'uniform_{semantics}')
            h = groups.get(f'hotspot_{semantics}')
            if u is not None and h is not None and abs(u - h) > 0.01:
                raise SystemExit(f'{ratio_name} {semantics}: hit rate differs {u} vs {h}')
    for name in DATASETS:
        for ratio_name in ('95_5', '80_20'):
            for semantics in ('growth', 'replacement'):
                u = manifest[f'{name}_{ratio_name}_uniform_{semantics}']
                h = manifest[f'{name}_{ratio_name}_hotspot_{semantics}']
                if u['live_before'] != h['live_before']:
                    raise SystemExit(f'{name} {ratio_name} {semantics}: initial maps differ '
                                     f"{u['live_before']} vs {h['live_before']}")
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print('traces', len(manifest), 'hit-rate parity ok')


if __name__ == '__main__':
    main()
