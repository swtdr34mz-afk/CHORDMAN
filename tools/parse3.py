import json, re
W=json.load(open('words.json'))
NOTE=re.compile(r'^([A-G])([#b]?)(-?\d)$')
COLS=[137.5,167.3,201.5,233.0,269.3,300.4,335.6,366.7,400.7,435.8,470.6,510.8]
markers=[]
for pi in range(23,32):
    ws=[w for w in W if w[0]==pi]
    htop=max(w[1] for w in ws if w[3] in('C´','D´','A´'))
    nums=[w for w in ws if w[1]>htop+3 and w[1]<780 and w[2]<80 and re.match(r'^\d+$',w[3])]
    for w in nums:
        if w[3]=='0': continue
        v=int(w[3]); 
        if pi==31 and v==10: v=100
        markers.append((pi,w[1],v))
sets={}
names={}
lineok={}
for pi,top,x,t in W:
    if x<125: continue
    k=(pi,round(top,1))
    lineok[k]=lineok.get(k,True) and bool(NOTE.match(t))
for pi,top,x,t in W:
    if pi>31 or x<125: continue
    ws=[w for w in W if w[0]==pi]
    m=[mm for mm in markers if mm[0]==pi]
    if not m: continue
    if NOTE.match(t) and lineok[(pi,round(top,1))]:
        mk=min(m,key=lambda mm:abs(mm[1]-top))
        c=min(range(12),key=lambda i:abs(COLS[i]-x))
        sets.setdefault(mk[2],[[] for _ in range(12)])[c].append((top,t))
SEMI={'C':0,'D':2,'E':4,'F':5,'G':7,'A':9,'B':11}
def midi(t):
    m=NOTE.match(t); n=SEMI[m.group(1)]+(1 if m.group(2)=='#' else -1 if m.group(2)=='b' else 0)
    return 12*(int(m.group(3))+1)+n
out={}
for k in sorted(sets):
    out[k]=[sorted(midi(t) for top,t in col) for col in sets[k]]
    cnt=[len(c) for c in out[k]]
    if min(cnt)<2 or max(cnt)>4: print('odd',k,cnt)
print(len(out), sorted(out)[:3], sorted(out)[-3:])
json.dump(out,open('chords.json','w'))
print(out[1]); print(out[100])
# names: non-note lines, assigned to the set whose first note row comes next below (same page)
first={}
for pi,top,x,t in W:
    if pi>31 or x<125 or not (NOTE.match(t) and lineok[(pi,round(top,1))]): continue
    m=[mm for mm in markers if mm[0]==pi]
    if not m: continue
    mk=min(m,key=lambda mm:abs(mm[1]-top))
    first[mk[2]]=min(first.get(mk[2],(pi,9e9)), (pi,top))
nm={}
for pi,top,x,t in W:
    if pi>31 or pi<23 or x<125 or lineok[(pi,round(top,1))]: continue
    ws=[w for w in W if w[0]==pi]
    htop=max(w[1] for w in ws if w[3] in('C´','D´','A´'))
    if top<=htop+3 or top>780: continue
    cands=[(f[1],k) for k,f in first.items() if f[0]==pi and f[1]>top]
    if not cands: continue
    k=min(cands)[1]
    c=min(range(12),key=lambda i:abs(COLS[i]-x))
    nm.setdefault(k,[[] for _ in range(12)])[c].append((top,t))
names={k:[''.join(t for top,t in sorted(col)) for col in v] for k,v in nm.items()}
style={}
for pi,top,x,t in W:
    if pi>31 or not (75<x<125): continue
    m=[mm for mm in markers if mm[0]==pi]
    if not m: continue
    mk=min(m,key=lambda mm:abs(mm[1]-top))
    if abs(mk[1]-top)<14: style.setdefault(mk[2],[]).append((top,x,t))
style={k:' '.join(t for top,x,t in sorted(v,key=lambda a:(round(a[0]/4),a[1]))) for k,v in style.items()}
for k in range(1,101): print(k, style.get(k,'?'), names.get(k,'?'))
json.dump({'notes':out,'names':names,'style':style},open('j6.json','w'))
