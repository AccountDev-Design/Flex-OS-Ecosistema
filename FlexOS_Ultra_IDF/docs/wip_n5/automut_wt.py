import re, random, subprocess, sys
ROOT="/tmp/claude-0/sp/wt/FlexOS_Ultra_IDF"
target, runner, nmax, seed = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
lo = int(sys.argv[5]) if len(sys.argv) > 5 else 0
hi = int(sys.argv[6]) if len(sys.argv) > 6 else 10**9
F=ROOT+'/'+target
orig=open(F).read()
lines=orig.split('\n')
ops=[(r'(?<![<>=!-])<(?![<=])', '<='), (r'<=', '<'), (r'(?<![-<>=])>(?![>=])', '>='), (r'>=', '>'), (r'==', '!='), (r'!=', '=='), (r'&&', '||'), (r'\|\|', '&&'),
     (r'(?<=[\w\)\] ]) \+ (?=[\w\(])', ' - '), (r'(?<=[\w\)\] ]) - (?=[\w\(])', ' + ')]
cands=[]
for i,l in enumerate(lines):
    if i+1 < lo or i+1 > hi: continue
    s=l.strip()
    if not s or s.startswith('//') or s.startswith('#') or s.startswith('*') or s.startswith('/*') or s.startswith('static const') or s.startswith('case ') or s.startswith('FST_') or 'snprintf' in s or 'add_text' in s and '"' in s and False:
        continue
    code=l.split('//')[0]
    for pat,rep in ops:
        for m in re.finditer(pat, code):
            cands.append((i,m.start(),m.end(),rep))
    for m in re.finditer(r'(?<![\w.%"\\x])(\d+)(?![\w."x%])', code):
        n=int(m.group(1))
        if n>=1 and not re.search(r'(RGB565|0x)', code):
            cands.append((i,m.start(1),m.end(1),str(n+1)))
random.seed(seed)
random.shuffle(cands)
cands=cands[:nmax]
res={'MUERTO':0,'SOBREVIVE':0,'INVALIDO':0}
surv=[]
try:
    for (i,a,b,rep) in cands:
        l=lines[i]
        code=l
        new=l[:a]+rep+l[b:]
        mut=lines[:i]+[new]+lines[i+1:]
        open(F,'w').write('\n'.join(mut))
        try:
            r=subprocess.run(runner,shell=True,cwd=ROOT+'/tests/host',capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=300)
            out=r.stdout+r.stderr
        except subprocess.TimeoutExpired:
            out='fails=1 timeout'
        m=re.search(r'checks=(\d+) fails=(\d+)',out)
        if not m:
            if 'error' in out or 'Error' in out: res['INVALIDO']+=1
            else: res['MUERTO']+=1   # crash / sanitizador
            continue
        if int(m.group(2))>0: res['MUERTO']+=1
        else:
            res['SOBREVIVE']+=1; surv.append((i+1,l.strip()[:110],new.strip()[:110]))
finally:
    open(F,'w').write(orig)
print(target, res)
for s in surv: print('  SOBREVIVE linea %d:\n    %s\n -> %s'%s)
