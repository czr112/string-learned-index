"""Verify the pinned evaluator's source files without modifying its checkout."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST=ROOT/'docs/dependencies/benchmark_94ead07a_files.json'

def verify(snapshot, manifest_path):
    manifest_bytes=manifest_path.read_bytes()
    manifest=json.loads(manifest_bytes)
    errors=[]
    for entry in manifest['files']:
        relative=Path(entry['path'])
        if relative.is_absolute() or '..' in relative.parts:
            raise ValueError('manifest path must remain inside the snapshot')
        source=(snapshot/relative).resolve()
        if not source.is_relative_to(snapshot):
            raise ValueError('snapshot member escapes its root')
        if not source.is_file():
            errors.append({'path':entry['path'],'error':'missing'})
            continue
        data=source.read_bytes()
        header=f'blob {len(data)}\0'.encode('ascii')
        actual=hashlib.sha1(header+data).hexdigest()
        if actual!=entry['git_blob_sha1'] or len(data)!=entry['bytes']:
            errors.append({'path':entry['path'],'error':'content_mismatch','actual_git_blob_sha1':actual})
    return {'status':'passed' if not errors else 'failed','repository':manifest['repository'],
            'commit':manifest['commit'],'source_files':len(manifest['files']),
            'manifest_sha256':hashlib.sha256(manifest_bytes).hexdigest(),
            'verifier_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'errors':errors,
            'scope':'source identity only; no build, correctness, or performance claim'}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot',required=True,type=Path)
    parser.add_argument('--manifest',type=Path,default=DEFAULT_MANIFEST)
    parser.add_argument('--report',type=Path)
    args=parser.parse_args()
    result=verify(args.snapshot.resolve(),args.manifest.resolve())
    text=json.dumps(result,indent=2,sort_keys=True)+'\n'
    if args.report:
        with args.report.open('x',encoding='utf-8') as output:
            output.write(text)
    print(text,end='')
    return 0 if result['status']=='passed' else 1

if __name__=='__main__':
    raise SystemExit(main())
