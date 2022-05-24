import os,sys,glob,hashlib

firms=glob.glob("FIRMS/*.firm")

out=b""

for i in firms:
    with open(i,"rb") as f:
        hash=hashlib.sha256(f.read(0x100)).digest()[:8]
    filename=os.path.basename(i)
    assert(len(filename) < 0x28)
    filename=filename+("\x00"*(0x28-len(filename)))
    out+=(hash+filename.encode('ascii'))
    
with open("data/firms.bin","wb") as f:
    f.write(out)