import re,sys,subprocess
def subseq_mask(base, side):
    """Return mask of base lines kept by side if side is a subsequence of base, else None."""
    mask=[False]*len(base); j=0
    for i,l in enumerate(base):
        if j<len(side) and base[i]==side[j]: mask[i]=True; j+=1
    return mask if j==len(side) else None
pat=re.compile(r'<<<<<<< [^\n]*\n(.*?)\|\|\|\|\|\|\| [^\n]*\n(.*?)=======\n(.*?)>>>>>>> [^\n]*\n',re.S)
report={}
for f in sys.argv[1:]:
    s=open(f).read(); done=[0,0]
    def rep(m):
        ours=m.group(1).splitlines(True); base=m.group(2).splitlines(True); theirs=m.group(3).splitlines(True)
        mo=subseq_mask(base,ours); mt=subseq_mask(base,theirs)
        if mo is None or mt is None:
            done[1]+=1; return m.group(0)
        done[0]+=1
        return ''.join(b for b,a,c in zip(base,mo,mt) if a and c)
    s2=pat.sub(rep,s); open(f,'w').write(s2); report[f]=tuple(done)
for f,(a,b) in report.items(): print(f"{a} resolved, {b} left  {f}")
