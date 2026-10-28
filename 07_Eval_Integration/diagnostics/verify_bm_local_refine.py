"""Recompute the registered work/space gates from integers, without importing the runner."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent
CELLS = ['english_words', 'memetracker_urls', 'memetracker_phrases', 'tranco64_norepl']


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p): return json.loads(p.read_text(encoding='utf-8'))


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('output'); args = parser.parse_args()
    out = Path(args.output).resolve()
    ident = read(out / 'identity.json'); normal = read(out / 'counts.json')
    san = read(out / 'counts_sanitized.json'); decision = read(out / 'decision.json')
    assert [r['cell'] for r in normal] == CELLS and normal == san
    assert not (out / 'failure.json').exists()
    for path, digest in ident['sources'].items():
        assert sha(WORKSPACE / path) == sha(out / 'frozen_sources' / path) == digest, ('source drift', path)
    for path, digest in ident['artifacts'].items(): assert sha(out / path) == digest
    for item in ident['inputs'].values():
        assert sha(WORKSPACE / item['rows_path']) == item['rows_sha256']
        assert sha(WORKSPACE / item['queries_path']) == item['queries_sha256']
    archive = read(out / 'log_archive.json')
    assert sha(out / 'commands.log.gz') == archive['gzip_sha256']
    assert hashlib.sha256(gzip.decompress((out / 'commands.log.gz').read_bytes())).hexdigest() == archive['raw_sha256']
    for r in normal:
        assert r['mismatch'] == r['query_new_calls'] == 0
        assert r['checks'] > r['rows'] + r['queries'] and r['fixture_checks'] > 0
        assert r['rows'] == ident['inputs'][r['cell']]['rows']
        assert r['queries'] == ident['inputs'][r['cell']]['queries']
        assert r['pages'] == (r['rows']+2047)//2048
        assert r['memory']['candidate'] - r['memory']['baseline'] == r['memory']['added']
        assert r['memory']['local_model_segments'] <= r['memory']['added']
        assert r['family_fences'] >= 4*r['families']
        for arm in ('current', 'refined', 'family_directory', 'base', 'directory'):
            w = r[arm]
            assert w['queries'] == r['queries']
            assert w['family_hits'] <= w['family_encodes'] <= r['queries']
            assert w['local_bm_calls'] <= w['family_hits']
        c = r['cell']
        assert abs(decision['fence_reduction'][c] - (1-r['refined']['fence_compares']/r['current']['fence_compares'])) < 1e-12
        assert abs(decision['metadata_added_over_baseline'][c] - r['memory']['added']/r['memory']['baseline']) < 1e-12
    main = next(r for r in normal if r['cell'] == 'memetracker_urls')
    enough = 4*main['refined']['fence_compares'] <= 3*main['current']['fence_compares']
    tags = all(r['refined']['blocks'] <= r['current']['blocks'] for r in normal)
    keys = all(r['refined']['key_compares'] <= r['current']['key_compares'] for r in normal)
    space = all(20*r['memory']['added'] <= r['memory']['baseline'] for r in normal)
    assert decision['main_25_percent'] == enough
    assert decision['blocks_never_increase'] == tags
    assert decision['key_verification_never_increases'] == keys
    assert decision['metadata_within_5_percent'] == space
    assert decision['entry_passes'] == (enough and tags and keys and space)
    assert decision['candidate_closed'] == (not decision['entry_passes'])
    assert decision['timing_performed'] is decision['production_adopted'] is decision['default_changed'] is False
    result = {'status': 'verified', 'cells': len(normal), 'sanitized_cells': len(san),
              'checks_per_build': sum(r['checks'] for r in normal),
              'entry_passes': decision['entry_passes'], 'fence_reduction': decision['fence_reduction'],
              'metadata_added_over_baseline': decision['metadata_added_over_baseline'],
              'source_files': len(ident['sources']),
              'checks': ['live_and_frozen_sources', 'artifacts', 'inputs', 'archive', 'integer_counters', 'sanitized', 'capacity', 'decision']}
    (out / 'verification.json').write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(result, ensure_ascii=False))


if __name__ == '__main__': main()
