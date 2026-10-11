import json
for f in ('adapters/our_index/benchmark-adapter.json', 'adapters/our_index_lits/benchmark-adapter.json'):
    d = json.load(open(f))
    si = d['source_identity']
    print(f)
    print('  index  =', si['index_hpp_sha256'])
    print('  switch =', si['switches_hpp_sha256'])
    print('  cand   =', si['candidate_switches_off'])
