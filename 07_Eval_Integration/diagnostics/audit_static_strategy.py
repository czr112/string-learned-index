"""Read-only data audit for the 2026-10-18 roadmap; no timing or index changes."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rows_from(path):
    data = path.read_bytes()
    assert data[:8] == b'SIBROW1\0'
    count, = struct.unpack_from('<Q', data, 8)
    pos, rows = 16, []
    for _ in range(count):
        n, = struct.unpack_from('<Q', data, pos)
        pos += 8
        key = data[pos:pos+n]
        pos += n
        value, = struct.unpack_from('<Q', data, pos)
        pos += 8
        rows.append((key, value))
    assert pos == len(data) and all(rows[i-1][0] < rows[i][0] for i in range(1, count))
    return rows


def lcp(a, b):
    i = 0
    while i < min(len(a), len(b)) and a[i] == b[i]:
        i += 1
    return i


def coordinate(key):
    x = 0
    for i in range(7):
        x = (x << 9) | (key[i]+1 if i < len(key) else 0)
    return x


def distribution(values):
    values = sorted(values)
    if not values:
        return {'count': 0}
    return {'count': len(values), 'min': values[0], 'p50': values[(len(values)-1)//2],
            'p99': values[int((len(values)-1)*.99)], 'max': values[-1]}


def geometry(rows):
    coverage, spans, collision_lcps, per_region = 0, [], [], []
    one_coordinate_regions = 0
    for first in range(0, len(rows), 2048):
        keys = [r[0] for r in rows[first:first+2048]]
        prefix = lcp(keys[0], keys[-1])
        suffixes = [k[prefix:] for k in keys]
        fence_xs = [coordinate(s) for s in suffixes[::16]]
        runs, begin = {}, 0
        while begin < len(fence_xs):
            end = begin+1
            while end < len(fence_xs) and fence_xs[end] == fence_xs[begin]:
                end += 1
            # Equal-coordinate queries may also precede the first fence in a run.
            span = end-begin+(begin > 0)
            runs[fence_xs[begin]] = span
            spans.append(span)
            begin = end
        one_coordinate_regions += len(runs) == 1
        count, family = 0, {}
        for suffix in suffixes:
            x = coordinate(suffix)
            if runs.get(x, 0) > 3:
                count += 1
                if x not in family:
                    family[x] = [suffix, suffix]
                else:
                    family[x][1] = suffix
        coverage += count
        collision_lcps.extend(lcp(a, b) for a, b in family.values())
        per_region.append(count/len(keys))
    return {'regions': len(per_region), 'blocks': (len(rows)+15)//16,
            'coordinate_ambiguous_rows': coverage, 'coordinate_ambiguous_row_fraction': coverage/len(rows),
            'ambiguous_fraction_by_region': distribution(per_region),
            'regions_with_one_fence_coordinate': one_coordinate_regions,
            'equal_coordinate_candidate_span': distribution(spans),
            'additional_lcp_in_wide_families': distribution(collision_lcps),
            'meaning': 'Equal-coordinate byte-fence predecessor ambiguity, including previous block. Data geometry only; not the actual trained certificate or runtime query frequency.'}


def query_coverage(path, rows):
    data = path.read_bytes()
    assert data[:8] == b'SIBQRY1\0'
    n, = struct.unpack_from('<Q', data, 8)
    pos, reference, hits, unique = 16, dict(rows), 0, set()
    for _ in range(n):
        present, value, length = struct.unpack_from('<BQQ', data, pos)
        pos += 17
        key = data[pos:pos+length]
        pos += length
        assert present in (0, 1) and (key in reference) == bool(present)
        if present:
            assert reference[key] == value
            hits += 1
            unique.add(key)
    assert pos == len(data)
    return {'queries': n, 'hits': hits, 'unique_hit_keys': len(unique),
            'fraction_of_stored_keys_hit': len(unique)/len(rows), 'sha256': sha(path)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', default='strategy_audit_20261018')
    args = parser.parse_args()
    assert Path(args.output).name == args.output
    out = ROOT/'results'/args.output
    out.mkdir(exist_ok=False)
    core = WORKSPACE/'02_Code_and_Experiments/validation_e25_20260918/static_physical_index.hpp'
    assert sha(core) == 'bb4befc3fa1a3ad5b65089d8d52339249d6f31e24e22dedb7a6d2d531d58e089'
    prior = ROOT/'results/static_interface_20261012c'
    cases = []
    for corpus, scale in [('tranco', 'full'), ('tranco', '64k'), ('mixed', 'full'), ('skew', 'full')]:
        path = ROOT/('results/matrix2_'+corpus)/'interchange/rows.bin' if scale == 'full' else prior/'tranco_64k_rows.bin'
        rows = rows_from(path)
        item = {'corpus': corpus, 'scale': scale, 'rows': len(rows),
                'rows_sha256': sha(path), 'key_length': distribution([len(k) for k, _ in rows]),
                'geometry': geometry(rows), 'coverage': {}}
        for n in (10000, 100000):
            q = prior/(f'{corpus}_{scale}_{n}_queries.bin')
            item['coverage'][str(n)] = query_coverage(q, rows)
        cases.append(item)
    result = {'date': '2026-10-18', 'base_commit': '564df24461e70cc0a4270351a42d807a8f9902a9',
              'audit_source_sha256': sha(Path(__file__)), 'core_sha256': sha(core),
              'region_keys': 2048, 'block_entries': 16, 'coordinate_symbols': 7,
              'scope': 'existing bytes and frozen S3 inputs only; no benchmarks, no model fitting, no production mutations',
              'cases': cases}
    (out/'summary.json').write_text(json.dumps(result, indent=2)+'\n')
    for c in cases:
        print(c['corpus'], c['scale'], 'ambiguous rows', round(c['geometry']['coordinate_ambiguous_row_fraction'], 4),
              'unique coverage', [round(v['fraction_of_stored_keys_hit'], 4) for v in c['coverage'].values()], flush=True)


if __name__ == '__main__':
    main()
