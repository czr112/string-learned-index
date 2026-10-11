import json, glob

MB = 1024.0 * 1024.0

def load(f):
    return [json.loads(l) for l in open(f) if l.strip().startswith('{')]

print('=== C2: shrink-to-peak, new-peak rate vs reclaimable Term A ===')
for f in sorted(glob.glob('results/main/branchc2_*.jsonl')):
    kind = f.split('_')[-1].split('.')[0]
    rows = load(f)
    agg = [r for r in rows if r.get('t') == 'agg' and r['population'] == 'nonempty']
    print()
    print('--- %s (non-empty population) ---' % kind)
    print('%-8s %-13s %-5s %10s %9s %9s %9s %11s %11s %8s' % (
        'workload', 'phase', 'frac', 'cap_bytes', 'A_bytes', 'iv_peaks',
        'iv_capchg', 'iv_peak_copy', 'iv_cc_copy', 'extra_MB'))
    for r in agg:
        extra = r['iv_new_peak_copy_bytes'] - r['iv_cap_change_copy_bytes']
        print('%-8s %-13s %-5s %10d %9d %9d %9d %11d %11d %8.2f' % (
            r['workload'], r['phase'], r['frac'], r['sum_cap_bytes'], r['sum_A_bytes'],
            r['iv_new_peak_events'], r['iv_cap_change_events'],
            r['iv_new_peak_copy_bytes'], r['iv_cap_change_copy_bytes'],
            extra / MB))
    # totals over the maintenance intervals only (skip the build-phase 'before' rows)
    for wl in ('replace', 'decline'):
        sel = [r for r in agg if r['workload'] == wl and r['phase'] != 'before']
        peaks = sum(r['iv_new_peak_events'] for r in sel)
        cc = sum(r['iv_cap_change_events'] for r in sel)
        pb = sum(r['iv_new_peak_copy_bytes'] for r in sel)
        cb = sum(r['iv_cap_change_copy_bytes'] for r in sel)
        last = [r for r in agg if r['workload'] == wl][-1]
        print('  %s MAINTENANCE TOTAL: peaks=%d (shipped growth=%d, extra=%d) copy=%.1f MB '
              '(shipped %.1f MB, EXTRA %.1f MB) vs reclaimable A=%.2f MB at the end'
              % (wl, peaks, cc, peaks - cc, pb / MB, cb / MB, (pb - cb) / MB,
                 last['sum_A_bytes'] / MB))
