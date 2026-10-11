import io, json, sys

new_hash = 'a5f042bb849f430ecbd571153e909fc4265db49b4abfbb0da47b849b19aea892'
for f in ('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/adapters/our_index/benchmark-adapter.json',
          '/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/adapters/our_index_lits/benchmark-adapter.json'):
    s = io.open(f, encoding='utf-8').read()
    old_line = '    "index_hpp_sha256": "ec65540c0489bf9b798d3a3fb61fce054a8fc0723fbcee72c6c6889146e9ba8c",'
    new_line = '    "index_hpp_sha256": "%s",' % new_hash
    n = s.count(old_line)
    if n != 1:
        sys.stderr.write('index hash line: count=%d in %s\n' % (n, f)); sys.exit(2)
    s = s.replace(old_line, new_line)

    old_note = io.open(f, encoding='utf-8').read()
    i = s.find('"index_hpp_note":')
    j = s.find('\n', i)
    new_note = ('"index_hpp_note": "updated 2026-10-10 (branch C-2 round): index.hpp 5,733 lines, '
                'DIAGNOSTIC COUNTERS ONLY were added since the compact-routing integration '
                '(the shrink-to-peak trackers live behind E25_DIAG). No shipped-path behaviour changed; '
                'the four earlier identity steps were 5423 -> 5593 (routing) -> 5629 (low-activity switch) '
                '-> 5673 (low-activity counters) -> 5733 (C-2 trackers). '
                'Evidence: 02_Code_and_Experiments/validation_e25_20260918/RESULTS_C2_PEAK_ENTRY.md",')
    s = s[:i] + new_note + s[j:]
    json.loads(s)   # validate before writing
    io.open(f, 'w', encoding='utf-8', newline='').write(s)
    print('UPDATED', f)
