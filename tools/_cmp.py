"""Dev-only: compare one rebuilt run against the recorded capacity record."""
import json
import sys

path = sys.argv[1]
capacity = json.load(open(path))
record = [x for x in capacity if x['arm'] == 'bm_base' and x['cell'] == 'english_words' and x['process'] == 0][0]
print(json.dumps({k: record[k] for k in ('checksum', 'hits', 'semantic_checks', 'self_report',
                                         'input_heap_bytes', 'build_ns')}, indent=1))
