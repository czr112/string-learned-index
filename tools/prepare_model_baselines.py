"""Fill the pinned model checkout's unmapped baseline directories, without replacing files."""
from pathlib import Path
import hashlib
import shutil

ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / '02_Code_and_Experiments/Learned-Index-Model'


def copy_missing(source, destination):
    if not source.is_dir():
        raise SystemExit(f'Missing dependency: {source}; run git submodule update --init first.')
    for original in source.rglob('*'):
        relative = original.relative_to(source)
        if any(part in {'.git', 'build', '__pycache__'} for part in relative.parts):
            continue
        if not original.is_file():
            continue
        target = destination / relative
        if target.exists():
            if hashlib.sha256(original.read_bytes()).digest() != hashlib.sha256(target.read_bytes()).digest():
                raise SystemExit(f'Existing baseline differs; no replacement performed: {target}')
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(original, target)


def main():
    if not (MODEL / 'src').is_dir():
        raise SystemExit('Model checkout missing; run git submodule update --init first.')
    copy_missing(ROOT / '02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart',
                 MODEL / 'related_works/libart')
    copy_missing(ROOT / '07_Eval_Integration/hot-src', MODEL / 'related_works/hot')
    print('Pinned model baseline dependencies are ready.')


if __name__ == '__main__':
    main()
