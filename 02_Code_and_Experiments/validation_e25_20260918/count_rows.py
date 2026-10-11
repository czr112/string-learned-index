import json
from collections import Counter
rows = [json.loads(l) for l in open('results/main/write_cost_diag_20261010.jsonl') if l.strip().startswith('{')]
c = Counter((r['kind'], r['mode'], r['workload']) for r in rows)
for k, v in sorted(c.items()):
    print(k, v)
print('total rows', len(rows))