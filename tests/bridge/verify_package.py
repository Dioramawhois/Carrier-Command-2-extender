"""Read-only PE/export/import verification, no DLL loading or game execution."""
import pathlib, struct, sys
def pe(path):
    data=path.read_bytes()
    u16=lambda p:struct.unpack_from('<H',data,p)[0]
    u32=lambda p:struct.unpack_from('<I',data,p)[0]
    header=u32(0x3c)
    assert data[header:header+4]==b'PE\0\0' and u16(header+4)==0x14c, path
    optional=header+24
    assert u16(optional)==0x10b,path
    sections=optional+u16(header+20)
    def offset(rva):
        for i in range(u16(header+6)):
            p=sections+i*40
            size,va,raw_size,raw=struct.unpack_from('<IIII',data,p+8)
            if va<=rva<va+max(size,raw_size):return raw+rva-va
        if rva<u32(optional+60):return rva
        raise AssertionError((path,rva))
    def string(rva):
        p=offset(rva);return data[p:data.index(b'\0',p)].decode('ascii')
    exports=[];imports=[]
    exp=u32(optional+96)
    if exp:
        p=offset(exp);count=u32(p+24);names=offset(u32(p+32))
        exports=[string(u32(names+i*4)) for i in range(count)]
    imp=u32(optional+104)
    if imp:
        p=offset(imp)
        while u32(p) or u32(p+12):imports.append(string(u32(p+12)).lower());p+=20
    return exports,imports
root=pathlib.Path(sys.argv[1])
expected={'DirectInput8Create','DllCanUnloadNow','DllGetClassObject','DllRegisterServer','DllUnregisterServer'}
assert not (root/'cc2x_native_bridge.dll').exists(), 'integrated package must not require or ship the standalone Native Bridge plugin'
for file in [root/'dinput8.dll',root/'diagnostics/cc2x_selftest.dll',root/'diagnostics/cc2x_hello.dll']:
    exports,imports=pe(file)
    assert expected==set(exports) if file.name=='dinput8.dll' else 'cc2x_plugin_load' in exports,(file,exports)
    assert not any('lua' in x or 'vcruntime' in x or 'msvcp' in x for x in imports),(file,imports)
    print('PASS PE Win32',file.name,'exports=',exports,'imports=',imports)
assert len(list(root.glob('*dinput8*.dll')))==1
