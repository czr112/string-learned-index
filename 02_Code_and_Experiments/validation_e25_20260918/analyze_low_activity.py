import json, glob
keys = ['arm','place_fail_bytes','hole_count_lt2','hole_count_eq0','hole_count_eq1',
        'low_activity_reclaimed','low_activity_n0','low_activity_n1','low_activity_bytes',
        'low_activity_helped','reclaim_widened','reclaim_widened_helped','subblock_reclaims',
        'subblock_reclaim_helped','overflow_inserts','compact_no_help','index_bytes']
for f in sorted(glob.glob('results/main/low_activity_diag_20261010_*.jsonl')):
    print('===', f)
    for line in open(f):
        line = line.strip()
        if not line.startswith('{'):
            continue
        r = json.loads(line)
        if r['frac'] != 2.0 or r['mode'] == 'insert':
            continue
        print('  mode=%s %s' % (r['mode'], ' '.join('%s=%s' % (k, r[k]) for k in keys)))
