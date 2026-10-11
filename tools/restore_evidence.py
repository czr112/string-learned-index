"""Restore archived diagnostic traces to their original paths, checking their SHA-256."""
from pathlib import Path
import gzip
import hashlib
import json

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / '04_Consolidation_and_Verification/archives/evidence_archives.json'


def digest(path):
    checksum = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            checksum.update(block)
    return checksum.hexdigest()


def inside_root(relative):
    path = (ROOT / relative).resolve()
    if not path.is_relative_to(ROOT):
        raise ValueError(f'Archive path outside repository: {relative}')
    return path


def main():
    entries = json.loads(CATALOG.read_text(encoding='utf-8'))['archives']
    for entry in entries:
        source = inside_root(entry['archive'])
        target = inside_root(entry['original'])
        if digest(source) != entry['archive_sha256']:
            raise SystemExit(f'Archive hash mismatch: {source}')
        if target.exists():
            if digest(target) != entry['original_sha256']:
                raise SystemExit(f'Existing evidence differs; no replacement performed: {target}')
            print(f'Already verified: {entry["original"]}')
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        # Validate decompressed bytes before creating the original path.
        checksum = hashlib.sha256()
        with gzip.open(source, 'rb') as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b''):
                checksum.update(block)
        if checksum.hexdigest() != entry['original_sha256']:
            raise SystemExit(f'Decompressed evidence hash mismatch: {source}')
        with gzip.open(source, 'rb') as stream, target.open('xb') as output:
            for block in iter(lambda: stream.read(1024 * 1024), b''):
                output.write(block)
        print(f'Restored: {entry["original"]}')


if __name__ == '__main__':
    main()
