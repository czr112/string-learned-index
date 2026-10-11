"""Exercise independent unsigned-byte reference and exact corpus routes under ASan/UBSan."""
import json
import subprocess
import sys
from run_lookup_structure import ROOT, UPSTREAM, digest, run

def runtime(command):
    result=subprocess.run(command,cwd=ROOT,text=True,capture_output=True,timeout=180)
    if result.returncode or result.stderr:
        raise RuntimeError(result.stderr or result.stdout)
    return result.stdout

def main():
    output=ROOT/"results/compact_routing_20261009"
    flags=["g++","-O1","-g","-std=c++17","-march=x86-64-v3",
           "-fsanitize=address,undefined","-fno-omit-frame-pointer"]
    test=ROOT/"diagnostics/build/compact_fence_router_test_san"
    if "--reuse-verified-builds" in sys.argv:
        previous=json.loads((output/"sanitizer.json").read_text())
        assert digest(test)==previous[0]["binary_sha256"]
    else:
        run(flags+["diagnostics/compact_fence_router_test.cpp","-o",str(test)])
    records=[dict(test="binary_domain_reference",output=runtime([str(test)]).strip(),stderr_empty=True,
                  binary_sha256=digest(test))]
    executable=ROOT/"diagnostics/build/lookup_routing_san"
    if "--reuse-verified-builds" in sys.argv:
        assert digest(executable)==previous[-1]["binary_sha256"]
        assert digest(ROOT/"diagnostics/compact_fence_router.h")==previous[-1]["router_sha256"]
        assert digest(ROOT/"diagnostics/compact_fence_router_test.cpp")==previous[-1]["reference_test_sha256"]
    else:
        run(flags+["-I"+str(UPSTREAM),"-Ilearned-index/src","diagnostics/lookup_routing.cpp",
                   "learned-index/src/unified/runner.cpp","-o",str(executable)])
    for corpus in ("mixed","skew","tranco"):
        path=ROOT/("results/matrix2_"+corpus)/"interchange"
        for backend in ("context16","lits"):
            text=runtime([str(executable),str(path/"rows.bin"),str(path/"queries.bin"),backend,"0"])
            gate=json.loads(text.strip())
            records.append(dict(test="corpus_routes",corpus=corpus,backend=backend,gate=gate,stderr_empty=True))
    records.append(dict(sanitizer_flags=flags,binary_sha256=digest(executable),
                        router_sha256=digest(ROOT/"diagnostics/compact_fence_router.h"),
                        reference_test_sha256=digest(ROOT/"diagnostics/compact_fence_router_test.cpp")))
    (output/"sanitizer.json").write_text(json.dumps(records,indent=2))
    print(json.dumps(records,indent=2))

if __name__=="__main__":main()
