"""Compare the controlled old/new emitted instruction sequences, resolving code relocations."""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import re
import subprocess


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser();parser.add_argument('results');args=parser.parse_args()
    root=Path(args.results).resolve();identity=json.loads((root/'timing/identity.json').read_text())
    binary=root/'build/bench';assert sha(binary)==identity['binary_sha256']
    symbols=subprocess.check_output(['nm','-S','-C',str(binary)],text=True)
    functions={}
    for line in symbols.splitlines():
        m=re.match(r'([0-9a-f]+) ([0-9a-f]+) \w View<(static_blocks|static_physical)::Index, (false|true)>::find\(',line)
        if m:functions[('old' if m[3]=='static_blocks' else 'new')+'_'+('checked' if m[4]=='true' else 'plain')]=(int(m[1],16),int(m[2],16))
    assert len(functions)==4
    normalized={}
    for name,(start,size) in functions.items():
        assembly=subprocess.check_output(['objdump','-d','-M','intel','--no-show-raw-insn',
            '--start-address='+hex(start),'--stop-address='+hex(start+size),str(binary)],text=True)
        result=[]
        for line in assembly.splitlines():
            m=re.match(r'\s*([0-9a-f]+):\s+(\S+)\s*(.*)',line)
            if not m:continue
            op,arg=m[2],m[3]
            # Resolve RIP-relative constants to their linked absolute target;
            # only internal branch targets are normalized relative to this function.
            comment=re.search(r'#\s*([0-9a-f]+)',arg)
            if comment:
                arg=re.sub(r'\[rip[+-]0x[0-9a-f]+\]','[absolute:'+comment[1]+']',arg.split('#',1)[0].strip())
            target=re.match(r'([0-9a-f]+)\s+<[^>]+>',arg)
            if target and (op.startswith('j') or op=='call'):
                destination=int(target[1],16)
                arg='self+'+hex(destination-start) if start<=destination<start+size else 'absolute:'+target[1]
            result.append((op+' '+arg.strip()).rstrip())
        normalized[name]=result
        (root/(name+'_normalized.asm')).write_text('\n'.join(result)+'\n')
    pairs=[]
    for kind in ['plain','checked']:
        old,new=normalized['old_'+kind],normalized['new_'+kind]
        diff=list(difflib.unified_diff(old,new,fromfile='old_'+kind,tofile='new_'+kind,lineterm=''))
        (root/(kind+'_codegen.diff')).write_text('\n'.join(diff)+('\n' if diff else ''))
        pairs.append({'wrapper':kind,'old_bytes':functions['old_'+kind][1],'new_bytes':functions['new_'+kind][1],
            'old_instruction_count':len(old),'new_instruction_count':len(new),'normalized_equal':old==new,
            'different_diff_lines':len(diff)})
    report={'binary_sha256':sha(binary),'comparison_source_sha256':sha(Path(__file__)),
        'normalization':'Internal jump offsets relative to function; RIP constants and external targets resolved to absolute linked addresses. Registers, operations, data offsets and nops retained.',
        'scope':'Textual emitted-code comparison, not a cycle/cache equivalence proof','pairs':pairs}
    (root/'codegen_comparison.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))


if __name__=='__main__':main()
