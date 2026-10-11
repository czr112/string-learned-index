import json, glob
print('mode arm corpus | place_fail_bytes slots | la n0 n1 bytes helped | overflow index_bytes')
for f in sorted(glob.glob('results/main/low_activity_diag_20261010_*.jsonl')):
    corpus = f.split('_')[-1].split('.')[0]
    for line in open(f):
        line = line.strip()
        if not line.startswith('{'):
            continue
        r = json.loads(line)
        if r['mode'] != 'insert':
            continue
        print('%-6s %-9s %-6s | %8d %6d | %3d %3d %3d %6d %6d | %6d %9d' % (
            r['mode'], r['arm'], corpus, r['place_fail_bytes'], r['place_fail_slots'],
            r['low_activity_reclaimed'], r['low_activity_n0'], r['low_activity_n1'],
            r['low_activity_bytes'], r['low_activity_helped'], r['overflow_inserts'],
            r['index_bytes']))
