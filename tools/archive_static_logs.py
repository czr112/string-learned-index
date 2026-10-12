"""Losslessly archive the two generated compiler/benchmark logs after checking bytes."""
import gzip
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

for name in ("static_physical_20261012a", "static_physical_20261012b"):
    directory = ROOT / "07_Eval_Integration/results" / name
    log = (directory / "commands.log").resolve()
    assert log.is_relative_to(ROOT.resolve()) and log.name == "commands.log"
    data = log.read_bytes()
    output = directory / "commands.log.gz"
    with output.open("xb") as raw:
        with gzip.GzipFile(filename="commands.log", mode="wb", fileobj=raw, mtime=0) as archive:
            archive.write(data)
    assert gzip.decompress(output.read_bytes()) == data
    metadata = {"original": "commands.log", "archive": output.name, "original_bytes": len(data),
                "original_sha256": hashlib.sha256(data).hexdigest(), "archive_bytes": output.stat().st_size,
                "archive_sha256": hashlib.sha256(output.read_bytes()).hexdigest(), "roundtrip_verified": True}
    (directory / "log_archive.json").write_text(json.dumps(metadata, indent=2) + "\n")
    # Only this explicitly checked, generated leaf file is removed; all bytes survive above.
    log.unlink()
    print(name, len(data), "->", output.stat().st_size, "bytes; verified")
