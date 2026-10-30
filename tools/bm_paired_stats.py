"""Pre-registered relative estimator; synthetic checks are not benchmark evidence."""
import math
import statistics
from run_bm_query_stability import interval

LABELS=('A0','A1','B0','B1')

def sequence(process,quad):
    a=quad%2;b=2+(quad//2)%2
    return [a,b,b,a] if (quad//4+process)%2==0 else [b,a,a,b]

def analyze(blocks,calibration):
    if len(blocks) not in (4,16):raise ValueError('paired independent-process count')
    estimates=[];replica_a=[];replica_b=[];parity=[];halves=[];absolute=[]
    for process,block in enumerate(blocks):
        rows=block['measurements']
        if block['phase']!=('calibrate' if calibration else 'confirm') or block['model16_measured']!=(not calibration):
            raise ValueError('paired phase/factory-role mismatch')
        if (block['process']!=process or len(rows)!=32 or
            {(r['quad'],r['position']) for r in rows}!={(q,p) for q in range(8) for p in range(4)}):
            raise ValueError('paired batch coverage')
        if len({r['lookups'] for r in rows})!=1 or len({r['checksum'] for r in rows})!=1:
            raise ValueError('paired count/checksum mismatch')
        if any(r['ns']<=0 or r['lookups']<=0 or r['cpu_before']!=block['cpu'] or r['cpu_after']!=block['cpu'] for r in rows):
            raise ValueError('paired nonpositive time or migration')
        previous=0
        for row in sorted(rows,key=lambda r:(r['quad'],r['position'])):
            start,end=row['envelope_start_ns'],row['envelope_end_ns']
            if start<previous or end<start or row['ns']>end-start:raise ValueError('paired batch envelope')
            previous=end
        by_label={label:[] for label in LABELS};effects=[]
        for quad in range(8):
            selected=sorted((r for r in rows if r['quad']==quad),key=lambda r:r['position'])
            if [r['state'] for r in selected]!=[LABELS[i] for i in sequence(process,quad)]:
                raise ValueError('paired ordering mismatch')
            values={side:[] for side in ('A','B')}
            for r in selected:
                y=math.log(r['ns']/r['lookups']);by_label[r['state']].append(y);values[r['state'][0]].append(y)
            effects.append(statistics.fmean(values['B'])-statistics.fmean(values['A']))
        if any(len(v)!=8 for v in by_label.values()):raise ValueError('paired replica coverage')
        estimates.append(statistics.fmean(effects))
        replica_a.append(statistics.fmean(by_label['A1'])-statistics.fmean(by_label['A0']))
        replica_b.append(statistics.fmean(by_label['B1'])-statistics.fmean(by_label['B0']))
        parity.append(statistics.fmean(effects[1::2])-statistics.fmean(effects[::2]))
        halves.append(statistics.fmean(effects[4:])-statistics.fmean(effects[:4]))
        absolute.append({s:[min(v),max(v)] for s,v in by_label.items()})
    if len({(b['cpu'],b['affinity']) for b in blocks})!=1:raise ValueError('paired affinity mismatch')
    checks={}
    for name,values,limit in (('replica_A',replica_a,1.05),('replica_B',replica_b,1.05),
                              ('relative_parity',parity,1.025),('relative_halves',halves,1.025)):
        value=interval(values);value['limit']=limit
        value['pass']=value['ci95'][0]>=1/limit and value['ci95'][1]<=limit;checks[name]=value
    primary=interval(estimates)
    if calibration:
        truth=dict(primary);truth['limit']=1.025;truth['pass']=truth['ci95'][0]>=1/1.025 and truth['ci95'][1]<=1.025
        checks['known_truth']=truth
    quality=all(v['pass'] for v in checks.values())
    if calibration:verdict='qualified' if quality else 'unqualified'
    elif not quality:verdict='measurement_unresolved'
    elif primary['ci95'][1]<=0.90:verdict='gain_at_least10pct'
    elif primary['ci95'][1]<=1.05:verdict='within5pct'
    elif primary['ci95'][0]>1.05:verdict='regression_over5pct'
    else:verdict='undecided'
    return {'primary_relative':primary,'quality_checks':checks,'quality_pass':quality,'verdict':verdict,
            'absolute_log_ns_per_lookup_ranges_diagnostic':absolute,'old_absolute_gate_reclassified':False}

def self_check():
    def synthetic(n,ratio,drift=0,calibration=False):
        result=[]
        for process in range(n):
            rows=[]
            for quad in range(8):
                for pos,index in enumerate(sequence(process,quad)):
                    value=1_000_000_000*(ratio if index>=2 else 1)*math.exp(drift*(4*quad+pos))
                    rows.append({'quad':quad,'position':pos,'state':LABELS[index],'ns':round(value),
                         'lookups':100000,'checksum':123,'cpu_before':0,'cpu_after':0,
                         'envelope_start_ns':(4*quad+pos)*10**12,'envelope_end_ns':(4*quad+pos)*10**12+round(value)+1000})
            result.append({'process':process,'cpu':0,'affinity':1,'measurements':rows,
                           'phase':'calibrate' if calibration else 'confirm','model16_measured':not calibration})
        return result
    truth=analyze(synthetic(4,1,calibration=True),True)
    if not truth['quality_pass'] or truth['primary_relative']['df']!=3:raise AssertionError('known unity qualification')
    # Independent worked value: B costs half of A. Linear log drift cancels by
    # the four fixed weights, without recomputing the estimator as the oracle.
    half=analyze(synthetic(16,0.5,0.001),False)
    if not half['quality_pass'] or abs(half['primary_relative']['ratio']-0.5)>1e-8 or half['primary_relative']['df']!=15:
        raise AssertionError('known half cost with ideal linear log drift')
    if analyze(synthetic(4,1.1,calibration=True),True)['quality_pass']:raise AssertionError('wrong known truth accepted')
    changing=synthetic(16,0.8)
    for block in changing:
        for row in block['measurements']:
            if row['state'].startswith('B') and row['quad']>=4:
                row['ns']=round(row['ns']*1.1);row['envelope_end_ns']=row['envelope_start_ns']+row['ns']+1000
    if analyze(changing,False)['quality_pass']:raise AssertionError('relative trend accepted')
    incomplete=synthetic(4,1,calibration=True);incomplete[0]['measurements'].pop()
    try:analyze(incomplete,True)
    except ValueError:pass
    else:raise AssertionError('incomplete coverage accepted')
    return {'synthetic_checks':5,'status':'pass','real_benchmark_records':False}

if __name__=='__main__':
    import json
    print(json.dumps(self_check()))
