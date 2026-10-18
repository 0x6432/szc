#!/usr/bin/env python3
"""Debug enum conversion safety: szc exact panic, official panic with trace allowed.
No invalid input is ever run under ReleaseFast. Uses existing build-exe drivers.
"""
import argparse,json,signal,tempfile
from pathlib import Path
from compiler_process import run_compiler,run
MODES=('valid','hole','width','negative','big-low-alias','max-hole')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('szc');p.add_argument('zig');p.add_argument('--reference-only',action='store_true');p.add_argument('--fixture',type=Path,default=Path(__file__).with_name('enum_safety.zig'));p.add_argument('--report',type=Path);a=p.parse_args();source=a.fixture.read_text();rows=[];bad=[]
 marker='const selected_mode:u8=0;'
 if source.count(marker)!=1:p.error('fixture needs exactly one supported scalar mode marker')
 with tempfile.TemporaryDirectory(prefix='szc-enum-safety-')as directory:
  root=Path(directory);path=root/'test.zig'
  for index,label in enumerate(MODES):
   case_source=source.replace(marker,f'const selected_mode:u8={index};');path.write_text(case_source)
   for who,compiler in ([('zig',a.zig)]if a.reference_only else [('szc',a.szc),('zig',a.zig)]):
    exe=root/(who+'-'+label);cmd=[compiler,'build-exe',str(path),'-O','Debug','-femit-bin='+str(exe),'-j2']if who=='zig'else[compiler,'build-exe',str(path),'-o',str(exe)]
    b=run_compiler(cmd,case_source,f'enum safety case {index+1} {who}',timeout=120)
    if b.returncode:bad.append(who+' '+label+' compile failed\n'+b.stderr.decode(errors='replace'));continue
    r=run(['bash','-c','ulimit -c 0; exec "$1"','enum-safety',str(exe)],timeout=15);text=r.stderr.decode(errors='replace')
    if label=='valid':ok=r.returncode==0 and not r.stdout and not r.stderr
    else:ok=r.returncode==-signal.SIGABRT and not r.stdout and ((b'panic: invalid enum value' in r.stderr)if who=='zig'else r.stderr==b'invalid enum value\n')
    rows.append(dict(compiler=who,mode=label,source=case_source,status=r.returncode,stderr=text,ok=ok))
    if not ok:bad.append(who+' '+label+': '+str(r.returncode)+'\n'+text)
    if a.report:a.report.write_text(json.dumps(rows,indent=2)+'\n')
 print(f'enum safety: {len(rows)} executions; {len(bad)} failures')
 if bad:raise SystemExit('\n'.join(bad))
if __name__=='__main__':main()
