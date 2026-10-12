import argparse,struct,zipfile
p=argparse.ArgumentParser();p.add_argument('apk');a=p.parse_args()
count=0;bad=[]
with zipfile.ZipFile(a.apk) as z:
    assert z.testzip() is None
    for f in z.infolist():
        if not f.filename.endswith('.so'):continue
        b=z.read(f);assert b[:4]==b'\x7fELF'
        off=struct.unpack_from('<Q',b,32)[0];size,n=struct.unpack_from('<HH',b,54)
        align=[struct.unpack_from('<Q',b,off+i*size+48)[0] for i in range(n) if struct.unpack_from('<I',b,off+i*size)[0]==1]
        if min(align)<16384:bad.append(f.filename)
        count+=1
assert not bad,bad
print('PASS APK archive CRC and 16KB ELF alignment:',count,'shared libraries')
