import subprocess, json, sys
O="git-maniam"; PN="5"
def run(a): 
    r=subprocess.run(a,capture_output=True,text=True); 
    if r.returncode: print("ERR",r.stderr[:200],file=sys.stderr)
    return r.stdout
pid=json.loads(run(["gh","project","view",PN,"--owner",O,"--format","json"]))["id"]
fl=json.loads(run(["gh","project","field-list",PN,"--owner",O,"--format","json"]))["fields"]
st=[f for f in fl if f["name"]=="Status"][0]
opts={o["name"]:o["id"] for o in st["options"]}
items=json.loads(run(["gh","project","item-list",PN,"--owner",O,"--format","json","--limit","200"]))["items"]
by={}
for it in items:
    c=it.get("content") or {}
    if c.get("number"): by[c["number"]]=it
def title(n): return by[n]["title"] if n in by else ""
args=sys.argv[1:]
for a in args:
    key,status=a.split("=",1)
    # key: N, or 'wbs:0.1.' prefix
    if key.startswith("wbs:"):
        pre=key[4:]
        nums=[n for n,it in by.items() if (it.get("wBS") or "").startswith(pre)]
    else:
        nums=[int(key)]
    for n in nums:
        run(["gh","project","item-edit","--id",by[n]["id"],"--project-id",pid,"--field-id",st["id"],"--single-select-option-id",opts[status]])
        if status=="Done": run(["gh","issue","close",str(n),"--repo","git-maniam/M6E","--reason","completed"])
        print(n,status)
