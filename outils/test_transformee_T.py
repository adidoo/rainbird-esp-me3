import csv, sys
MASK=0x80718611
def T(x,n=13):
    for _ in range(n):
        fb=bin(x&MASK).count('1')&1
        x=((x<<1)|fb)&0xFFFFFFFF
        if x==0: x=0xFFFFFFFF
    return x
def Tinv_step(y):
    # y = (x<<1 | fb) ; x low 31 bits = y>>1 ; top bit of x chosen so parity(x&MASK)==y&1 (MASK has bit31)
    lo=y>>1
    fb=y&1
    p=bin(lo&MASK).count('1')&1
    return lo | ((p^fb)<<31)
def Tinv(y,n=13):
    for _ in range(n): y=Tinv_step(y)
    return y
assert T(0x12345678)==0x8ACF17EB, hex(T(0x12345678))
assert Tinv(0x8ACF17EB)==0x12345678
def unstuff(b):
    out=bytearray(); i=0
    while i<len(b):
        if b[i]==0x1B and i+1<len(b): out.append({1:0x1C,2:0x1B,3:0x1D}.get(b[i+1],b[i+1])); i+=2
        else: out.append(b[i]); i+=1
    return bytes(out)
rows=list(csv.DictReader(open(sys.argv[1])))
n=okB=okA=exactB=exactA=0; bad=[]
for r in rows:
    resp=unstuff(bytes.fromhex(r['resp'])[1:-1])  # len 00 86 ...
    if len(resp)<12 or resp[2]!=0x86: continue
    n+=1
    TA=resp[3:7]; B=int.from_bytes(resp[7:11],'big')
    tb=T(B).to_bytes(4,'big'); a=Tinv(int.from_bytes(TA,'big')).to_bytes(4,'big')
    reply=unstuff(bytes.fromhex(r['reply'])); chal=unstuff(bytes.fromhex(r['chal']))
    # damaged LNK2 captures: look for the whole T(B), otherwise >= 3 consecutive bytes
    eB = tb in reply; pB = eB or any(tb[i:i+3] in reply for i in (0,1))
    eA = a in chal;   pA = eA or any(a[i:i+3] in chal for i in (0,1))
    exactB+=eB; okB+=pB; exactA+=eA; okA+=pA
    if not pB and len(bad)<8: bad.append((r['fichier'],r['resp'],'T(B)='+tb.hex(),r['reply']))
print(f"handshakes exploitables : {n}")
print(f"réponse 07 contient T(B) : complet {exactB}, >=3 octets {okB}  ({100*okB/n:.1f} %)")
print(f"défi 06 = T^-1(réponse 86) : complet {exactA}, >=3 octets {okA}  ({100*okA/n:.1f} %)")
for b in bad: print(b)

# --- more tolerant measure: bytes of T(B) found in order (subsequence), vs control (shifted pairing)
def lcs(a,b):
    m=[[0]*(len(b)+1) for _ in range(len(a)+1)]
    for i in range(len(a)):
        for j in range(len(b)):
            m[i+1][j+1]=m[i][j]+1 if a[i]==b[j] else max(m[i][j+1],m[i+1][j])
    return m[-1][-1]
data=[]
for r in rows:
    resp=unstuff(bytes.fromhex(r['resp'])[1:-1])
    if len(resp)<12 or resp[2]!=0x86: continue
    data.append((T(int.from_bytes(resp[7:11],'big')).to_bytes(4,'big'), unstuff(bytes.fromhex(r['reply']))[1:],
                 Tinv(int.from_bytes(resp[3:7],'big')).to_bytes(4,'big'), unstuff(bytes.fromhex(r['chal']))[1:]))
from collections import Counter
for nom,i,j in (("T(B) dans la reponse 07",0,1),("T^-1(T(A)) dans le defi 06",2,3)):
    vrai=Counter(lcs(d[i],d[j]) for d in data)
    temoin=Counter(lcs(data[k][i],data[(k+7)%len(data)][j]) for k in range(len(data)))
    print(nom); print("  octets retrouves 0..4 :",[vrai[k] for k in range(5)]); print("  temoin (paires decalees):",[temoin[k] for k in range(5)])
