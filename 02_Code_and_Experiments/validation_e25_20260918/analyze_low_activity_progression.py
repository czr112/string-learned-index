import json
f = 'results/main/low_activity_diag_20261010_mixed.jsonl'
print('mode arm frac | fail_bytes lt2 eq0 eq1 | la n0 n1 bytes helped | overflow')
for line in open(f):
    line = line.strip()
    if not line.startswith('{'):
        continue
    r = json.loads(line)
    if r['mode'] != 'fifo':
        continue
    print('%-6s %-9s %-4s | %9d %7d %7d %5d | %4d %4d %4d %6d %6d | %6d' % (
        r['mode'], r['arm'], r['frac'], r['place_fail_bytes'], r['hole_count_lt2'],
        r['hole_count_eq0'], r['hole_count_eq1'], r['low_activity_reclaimed'],
        r['low_activity_n0'], r['low_activity_n1'], r['low_activity_bytes'],
        r['low_activity_helped'], r['overflow_inserts']))
