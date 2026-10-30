"""One human-confirmed idle-window calibration using byte-identical native binaries."""
from pathlib import Path
import argparse
import ctypes
from ctypes import wintypes
import datetime as dt
import json
import os
import shutil
import subprocess
import time
from run_bm_tx import ROOT,save,sha
import run_bm_query_native as native

PROTOCOL=ROOT/'docs/PROTOCOL_BM_NATIVE_IDLE_WINDOW_20261030.md'
UTC=dt.timezone.utc
def now():return dt.datetime.now(UTC)
def parse(value):
    value=dt.datetime.fromisoformat(value.replace('Z','+00:00'))
    if value.tzinfo is None or value.utcoffset()!=dt.timedelta(0):raise ValueError('UTC window timestamp required')
    return value

class WindowRun(native.NativeRun):
    def __init__(self,output,start,end,note):
        current=now()
        if start>current or end<=current or end<=start:raise ValueError('confirmed idle window is not active')
        self.end=end;self.deadline=time.monotonic()+(end-current).total_seconds()
        super().__init__(output)
        self.kernel=ctypes.WinDLL('kernel32',use_last_error=True)
        self.kernel.GetSystemTimes.argtypes=[ctypes.POINTER(wintypes.FILETIME)]*3
        self.kernel.GetSystemTimes.restype=wintypes.BOOL
        self.kernel.GetCurrentProcess.restype=wintypes.HANDLE
        self.kernel.GetCurrentThread.restype=wintypes.HANDLE
        self.kernel.GetPriorityClass.argtypes=[wintypes.HANDLE];self.kernel.GetPriorityClass.restype=wintypes.DWORD
        self.kernel.GetThreadPriority.argtypes=[wintypes.HANDLE];self.kernel.GetThreadPriority.restype=ctypes.c_int
        priority=self.kernel.GetPriorityClass(self.kernel.GetCurrentProcess())
        thread=self.kernel.GetThreadPriority(self.kernel.GetCurrentThread())
        if priority!=32 or thread!=0:raise ValueError('idle test launcher must have normal priority')
        self.report['window']={'confirmed_start_utc':start.isoformat(),'confirmed_end_utc':end.isoformat(),
            'human_note':note,'launcher_utc':current.isoformat(),'parent_process_class':priority,'parent_thread_priority':thread,
            'child_creation_class':subprocess.NORMAL_PRIORITY_CLASS,'system_counters_are_not_latency':True}

    def system_times(self):
        idle,kernel,user=wintypes.FILETIME(),wintypes.FILETIME(),wintypes.FILETIME()
        if not self.kernel.GetSystemTimes(ctypes.byref(idle),ctypes.byref(kernel),ctypes.byref(user)):
            raise OSError(ctypes.get_last_error(),'system times unavailable')
        def integer(x):return ((x.dwHighDateTime<<32)|x.dwLowDateTime)*100
        return {'idle_ns':integer(idle),'kernel_including_idle_ns':integer(kernel),'user_ns':integer(user)}

    def execute(self,args,name,env=None):
        args=list(map(str,args))
        if args[0] not in self.binaries:return super().execute(args,name,env)
        remaining=min((self.end-now()).total_seconds(),self.deadline-time.monotonic())
        if remaining<=0:raise TimeoutError('confirmed idle window expired before '+name)
        if sha(args[0])!=self.binaries[args[0]] or any(sha(p)!=h for p,h in self.runtime.items()):
            raise ValueError('idle binary/runtime drift before execution')
        self.commands.append(args);save(self.output/'commands.json',self.commands)
        record={'start_utc':now().isoformat(),'timeout_seconds':remaining,'system_before':self.system_times()}
        self.report.setdefault('children',{})[name]=record;save(self.output/'report.json',self.report)
        try:
            with (self.output/name).open('w') as stream:
                result=subprocess.run(args,cwd=ROOT,env=env,stdout=stream,stderr=subprocess.STDOUT,
                    timeout=remaining,creationflags=subprocess.NORMAL_PRIORITY_CLASS)
            record['returncode']=result.returncode
            if result.returncode:raise RuntimeError(name+': exit '+str(result.returncode))
        except subprocess.TimeoutExpired:
            record['window_timeout']=True;raise TimeoutError('own test child stopped at idle-window end: '+name)
        finally:
            record.update(end_utc=now().isoformat(),system_after=self.system_times());save(self.output/'report.json',self.report)
        return (self.output/name).read_text()

def qualified_selection(path,digest,hashes,affinity):
    value=json.loads(path.read_text());directory=path.resolve().parent
    report=json.loads((directory/'report.json').read_text())
    expected={f'{c}_p{p:02d}.json' for c in native.PILOT for p in range(4)}
    if (report.get('status')!='complete' or report.get('phase')!='calibrate' or value['selected_ms']!=1000
        or value['report_sha256']!=sha(directory/'report.json') or value['source_closure_sha256']!=digest
        or value['protocol_sha256']!=sha(PROTOCOL) or value['affinity']!=list(affinity)
        or value['window']!=report['window'] or set(value['sample_hashes'])!=expected
        or json.loads((directory/'input_hashes.json').read_text())!=hashes):
        raise ValueError('idle calibration selection is unqualified or unbound')
    for name,h in value['sample_hashes'].items():
        if sha(directory/name)!=h:raise ValueError('idle calibration sample drift')
    for cell in native.PILOT:
        blocks=[json.loads((directory/f'{cell}_p{p:02d}.json').read_text()) for p in range(4)]
        actual=native.analyze(blocks)
        if actual!=report['results'][cell] or not actual['all_aa_pass']:raise ValueError('idle AA reanalysis failed')
    return value

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--window-start-utc',required=True)
    parser.add_argument('--window-end-utc',required=True)
    parser.add_argument('--human-note',required=True)
    parser.add_argument('--phase',choices=('calibrate','confirm'),default='calibrate')
    parser.add_argument('--selection',type=Path)
    args=parser.parse_args();run=WindowRun(args.output,parse(args.window_start_utc),parse(args.window_end_utc),args.human_note)
    for p in (Path(__file__).resolve(),PROTOCOL,Path(native.__file__).resolve(),native.PROTOCOL,
              Path(native.old_statistics.__file__).resolve(),native.old_statistics.PROTOCOL):run.sources[str(p)]=sha(p)
    try:
        prior=ROOT/'07_Eval_Integration/results/bm_native_query_calibration_20261030';metadata={}
        for name in ('report.json','identity.json','environment.json'):
            p=prior/name;frozen=subprocess.check_output(['git','-C',str(ROOT),'show','d337576:'+p.relative_to(ROOT).as_posix()])
            if p.read_bytes()!=frozen:raise ValueError('frozen native metadata drift')
            run.sources[str(p)]=sha(p);metadata[name]=json.loads(p.read_text())
        if metadata['report.json']['status']!='complete':raise ValueError('frozen native run incomplete')
        current=json.loads((run.output/'environment.json').read_text())
        if current['toolchain']!=metadata['environment.json']['toolchain']:raise ValueError('idle toolchain differs from frozen binary')
        identity=metadata['identity.json']
        for p,h in identity['closure'].items():
            if sha(p)!=h:raise ValueError('frozen compiled source/tool drift: '+p)
            run.sources[p]=h
        binaries={}
        for name in ('query_native.exe','contract_native.exe'):
            matches=[(Path(p),h) for p,h in identity['binaries'].items() if Path(p).name==name]
            if len(matches)!=1:raise ValueError('frozen binary coverage')
            original,digest=matches[0]
            if sha(original)!=digest:raise ValueError('frozen binary drift')
            target=run.output/name;partial=target.with_name(name+'.partial');shutil.copyfile(original,partial);os.replace(partial,target)
            if sha(target)!=digest:raise ValueError('idle binary copy drift')
            run.binaries[str(target)]=digest;run.sources[str(original)]=digest;binaries[name]=target
        save(run.output/'reused_identity.json',{'frozen_identity_sha256':sha(prior/'identity.json'),
            'binaries':run.binaries,'compiled_source_closure':identity['closure']})
        cases,hashes=native.frozen_inputs();save(run.output/'cases.json',cases);save(run.output/'input_hashes.json',hashes)
        run.report['contract']=run.execute([binaries['contract_native.exe']],'contract.log')
        if '99/99 checks passed' not in run.report['contract']:raise ValueError('idle public contract')
        modules=[x.removeprefix('NATIVE_RUNTIME ') for x in run.report['contract'].splitlines() if x.startswith('NATIVE_RUNTIME ')]
        if len(modules)!=1:raise ValueError('idle contract runtime record')
        run.verify_modules({'runtime_modules':json.loads(modules[0])})
        run.report.update(phase=args.phase,semantic={},results={});affinity=set()
        binary=binaries['query_native.exe']
        for cell,paths in sorted(cases.items()):
            record=json.loads(run.execute([binary,'check',*paths,0,1000],cell+'_check.json'));run.verify_modules(record)
            if not record['passed']:raise ValueError('idle semantics')
            run.report['semantic'][cell]=record;affinity.add((record['cpu'],record['affinity']))
        if len(affinity)!=1:raise ValueError('idle affinity changed')
        affinity=next(iter(affinity))
        if list(affinity)!=metadata['report.json']['selection']['affinity']:raise ValueError('idle CPU differs from original')
        digest=native.closure_digest(run)
        if args.phase=='confirm':
            if args.selection is None:raise ValueError('qualified idle selection required')
            chosen=qualified_selection(args.selection,digest,hashes,affinity)
            run.report['calibration_selection']=chosen;save(run.output/'selection.json',chosen)
        cells=native.PILOT if args.phase=='calibrate' else tuple(sorted(cases))
        processes=4 if args.phase=='calibrate' else 16
        save(run.output/'registration.json',{'protocol_sha256':sha(PROTOCOL),'source_closure_sha256':digest,
            'window':run.report['window'],'phase':args.phase,'fixed_floor_ms':1000,'processes':processes,
            'rounds':8,'cells':cells,'affinity':affinity})
        for cell in cells:
            blocks=[]
            for process in range(processes):
                record=json.loads(run.execute([binary,'run',*cases[cell],process,1000],f'{cell}_p{process:02d}.json'))
                run.verify_modules(record)
                if (record['cpu'],record['affinity'])!=affinity:raise ValueError('idle measured affinity')
                blocks.append(record)
            run.report['results'][cell]=native.analyze(blocks,args.phase=='confirm')
            save(run.output/'report.json',run.report);print(cell+': AA='+str(run.report['results'][cell]['all_aa_pass']),flush=True)
        if args.phase=='calibrate':
            run.report['selection']={'selected_ms':1000 if all(x['all_aa_pass'] for x in run.report['results'].values()) else None,
                'affinity':affinity,'protocol_sha256':sha(PROTOCOL),'source_closure_sha256':digest,'decision':'AA only',
                'window':run.report['window'],'sample_hashes':{f'{c}_p{p:02d}.json':sha(run.output/f'{c}_p{p:02d}.json') for c in native.PILOT for p in range(4)}}
        if any(sha(p)!=h for p,h in hashes.items()):raise ValueError('idle input drift at completion')
        run.finish()
        if args.phase=='calibrate':save(run.output/'selection.json',dict(run.report['selection'],report_sha256=sha(run.output/'report.json')))
        print(json.dumps({'status':'complete','phase':args.phase,'selected_ms':run.report.get('selection',{}).get('selected_ms')}),flush=True)
    except Exception as error:
        run.report.update(status='failed',error=str(error));save(run.output/'report.json',run.report);raise

if __name__=='__main__':main()
