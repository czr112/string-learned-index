import json

rows = [json.loads(l) for l in open('results/main/write_cost_diag_20261010.jsonl') if l.strip().startswith('{')]
calib = rows[0]['calib_ns']
CORPORA = ['mixed', 'skew', 'tranco']

def last(kind, mode, workload):
    sel = [r for r in rows if r['kind'] == kind and r['mode'] == mode and r['workload'] == workload]
    return sel[-1] if sel else None

def floor(r, f):
    c = r.get(f.replace('_ns', '_calls'), 0)
    return c * calib

def ms(ns):
    return ns / 1e6

print('calibration: %.2f ns per timer pair (= what ONE clocked call costs before it does any work)' % calib)
print()
print('=' * 104)
print('A. THE WRITE PATH, WHOLE (N=262144; last checkpoint: 200% for replace, 100% for insert)')
print('=' * 104)
print('%-7s %-8s %10s %10s | %11s %11s %11s %11s %11s' % (
    'corpus', 'mode', 'ops', 'live', 'Index.ins', '  locate', '  findloc', '  place', '  rebuild'))
for mode, wl in (('same', 'replace'), ('insert', 'insert'), ('grow', 'replace')):
    for k in CORPORA:
        r = last(k, mode, wl)
        if not r:
            continue
        print('%-7s %-8s %10d %10d | %11.2f %11.2f %11.2f %11.2f %11.2f' % (
            k, mode, r['ops'], r['live'],
            ms(r['w_insert_ns'] - floor(r, 'w_insert_ns')),
            ms(r['w_locate_ins_ns'] - floor(r, 'w_locate_ins_ns')),
            ms(r['w_findloc_ns'] - floor(r, 'w_findloc_ns')),
            ms(r['w_place_ns'] - floor(r, 'w_place_ns')),
            ms(r['w_rebuild_ns'] - floor(r, 'w_rebuild_ns'))))
    print()

print('=' * 104)
print('B. SHARES INSIDE Index::insert (corrected ms; the floor column says what this instrument')
print('   can even resolve: a phase whose time is under its floor is NOT measured)')
print('=' * 104)
print('%-7s %-8s %10s | %7s %7s %7s %7s %7s | %9s %9s %9s' % (
    'corpus', 'mode', 'ins_ms', 'locate', 'findloc', 'place', 'rebuild', 'other',
    'flr_loc', 'flr_find', 'flr_place'))
for mode, wl in (('same', 'replace'), ('insert', 'insert'), ('grow', 'replace')):
    for k in CORPORA:
        r = last(k, mode, wl)
        if not r:
            continue
        ins = r['w_insert_ns'] - floor(r, 'w_insert_ns')
        if ins <= 0:
            continue
        loc = r['w_locate_ins_ns'] - floor(r, 'w_locate_ins_ns')
        fin = r['w_findloc_ns'] - floor(r, 'w_findloc_ns')
        plc = r['w_place_ns'] - floor(r, 'w_place_ns')
        reb = r['w_rebuild_ns'] - floor(r, 'w_rebuild_ns')
        oth = ins - loc - fin - plc - reb
        print('%-7s %-8s %10.2f | %6.1f%% %6.1f%% %6.1f%% %6.1f%% %6.1f%% | %9.2f %9.2f %9.2f' % (
            k, mode, ms(ins), 100*loc/ins, 100*fin/ins, 100*plc/ins, 100*reb/ins, 100*oth/ins,
            ms(floor(r, 'w_locate_ins_ns')), ms(floor(r, 'w_findloc_ns')), ms(floor(r, 'w_place_ns'))))
    print()

print('=' * 104)
print('C. SHARES INSIDE Index::erase (corrected ms)')
print('=' * 104)
print('%-7s %-8s %10s | %7s %7s %7s %7s | %9s' % (
    'corpus', 'mode', 'era_ms', 'locate', 'Region', 'ov_era', 'other', 'flr_locate'))
for mode in ('same', 'grow'):
    for k in CORPORA:
        r = last(k, mode, 'replace')
        if not r:
            continue
        er = r['w_erase_ns'] - floor(r, 'w_erase_ns')
        if er <= 0:
            continue
        loc = r['w_locate_era_ns'] - floor(r, 'w_locate_era_ns')
        reg = r['erase_ns'] - floor(r, 'erase_ns')
        ove = r['ov_erase_ns'] - floor(r, 'ov_erase_ns')
        oth = er - loc - reg
        print('%-7s %-8s %10.2f | %6.1f%% %6.1f%% %6.1f%% %6.1f%% | %9.2f' % (
            k, mode, ms(er), 100*loc/er, 100*reg/er, 100*ove/er, 100*oth/er,
            ms(floor(r, 'w_locate_era_ns'))))
    print()

print('=' * 104)
print('D. THE FIVE KINDS, PER 1,000 OPERATIONS')
print('=' * 104)
print('%-7s %-8s | %10s %12s | %9s %11s %11s | %8s %10s | %9s %9s' % (
    'corpus', 'mode', '(a) primary', '(a) overflow', '(b) entry', '(b) ovshift_+', '(b) ovshift_-',
    '(c) grow_ev', '(c) grow_B', '(d) strings', '(e) all()rec'))
for mode, wl in (('same', 'replace'), ('insert', 'insert'), ('grow', 'replace')):
    for k in CORPORA:
        r = last(k, mode, wl)
        if not r:
            continue
        ops = max(1, r['ops'])
        f = 1000.0 / ops
        print('%-7s %-8s | %10.0f %12.0f | %9.0f %11.0f %11.0f | %8.1f %10.0f | %9.0f %9.0f' % (
            k, mode,
            r['k_suffix_bytes_primary']*f, r['k_suffix_bytes_overflow']*f,
            r['k_entry_shifts']*f, r['k_ov_shift_recs_ins']*f, r['k_ov_shift_recs_era']*f,
            r['k_cap_change_events']*f, r['k_cap_change_bytes']*f, r['k_string_allocs']*f,
            r['w_rall_records']*f))
    print()

print('=' * 104)
print('E. WHERE THE OVERFLOW RECORDS GO (per 1,000 ops): the placement outcome')
print('=' * 104)
print('%-7s %-8s %10s %12s %12s' % ('corpus', 'mode', 'attempts', 'ok(primary)', 'overflow'))
for mode, wl in (('same', 'replace'), ('insert', 'insert'), ('grow', 'replace')):
    for k in CORPORA:
        r = last(k, mode, wl)
        if not r:
            continue
        f = 1000.0 / max(1, r['ops'])
        print('%-7s %-8s %10.0f %12.0f %12.0f' % (
            k, mode, r['k_insert_attempts']*f, r['k_insert_ok']*f, r['k_overflow_inserts']*f))
    print()