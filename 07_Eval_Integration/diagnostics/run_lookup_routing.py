"""Same-state, single-variable A/B for the static compact fence prototype."""
import json
import os
from pathlib import Path
import statistics
from run_lookup_structure import ROOT, UPSTREAM, digest, run

def main():
    output=ROOT/"results/compact_routing_20261009"
    output.mkdir(exist_ok=False)
    binary=ROOT/"diagnostics/build/lookup_routing"
    sources=[ROOT/"diagnostics/lookup_routing.cpp",ROOT/"diagnostics/compact_fence_router.h",
             ROOT/"diagnostics/lookup_structure.cpp",Path(__file__),
             ROOT/"diagnostics/run_lookup_structure.py",ROOT/"learned-index/src/unified/runner.cpp"]
    sources+=list(UPSTREAM.rglob("*.hpp"))
    identity={str(p.relative_to(ROOT.parent)):digest(p) for p in sources}
    command=["g++","-O3","-std=c++17","-march=x86-64-v3","-I"+str(UPSTREAM),"-Ilearned-index/src",
             "diagnostics/lookup_routing.cpp","learned-index/src/unified/runner.cpp","-o",str(binary)]
    run(command);cpu=min(os.sched_getaffinity(0))
    metadata={"source_sha256":identity,"command":command,"binary_sha256":digest(binary),"cpu":cpu,
              "scope":"static-only prototype; borrowed immutable fence strings; no default changes",
              "protocol":"5 process blocks x 4 balanced repeats; same index; 100 query passes per arm"}
    raw=[];summary=[]
    for corpus in ("mixed","skew","tranco"):
        inputs=ROOT/("results/matrix2_"+corpus)/"interchange"
        metadata[corpus]={n:digest(inputs/n) for n in ("rows.bin","queries.bin")}
        for backend in ("context16","lits"):
            medians=[];gates=[]
            for process in range(5):
                rows=[json.loads(x) for x in run(["taskset","-c",str(cpu),str(binary),str(inputs/"rows.bin"),
                     str(inputs/"queries.bin"),backend,"4"]).splitlines()]
                gate=rows[0];gates.append(gate)
                measured=rows[1:];assert len(measured)==8
                assert len({x["checksum"] for x in measured})==1
                by_key={(x["repeat"],x["variant"]):x for x in measured}
                paired=[by_key[r,1]["elapsed_ns"]/by_key[r,0]["elapsed_ns"] for r in range(4)]
                medians.append(statistics.median(paired))
                for row in rows:row.update(corpus=corpus,backend=backend,process=process);raw.append(row)
            summary.append(dict(corpus=corpus,backend=backend,process_medians=medians,median=statistics.median(medians),
                                minimum=min(medians),maximum=max(medians),faster=sum(v<1 for v in medians),
                                auxiliary_bytes=gates[0]["auxiliary_bytes"],global_prefix_bytes=gates[0]["global_prefix_bytes"],
                                route_checks=gates[0]["route_checks"],queries_checked=gates[0]["queries_checked"]))
            (output/"raw.json").write_text(json.dumps(raw,indent=2))
            (output/"summary.json").write_text(json.dumps(summary,indent=2))
            print(corpus,backend,"done",flush=True)
    assert all(digest(ROOT.parent/p)==v for p,v in identity.items()),"source drift"
    (output/"identity.json").write_text(json.dumps(metadata,indent=2))

if __name__=="__main__":main()
