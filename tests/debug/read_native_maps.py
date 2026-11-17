"""Read only the three known input mapping tables in the running target."""
import ctypes, struct, subprocess
from ctypes import wintypes as w
kernel=ctypes.WinDLL('kernel32',use_last_error=True)
kernel.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];kernel.OpenProcess.restype=w.HANDLE
kernel.ReadProcessMemory.argtypes=[w.HANDLE,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t)]
kernel.CloseHandle.argtypes=[w.HANDLE]
pid=int(subprocess.check_output(['powershell','-NoProfile','-Command','(Get-Process -Name OROCHI_Z_TC).Id']).strip())
h=kernel.OpenProcess(0x10,False,pid)
if not h: raise ctypes.WinError(ctypes.get_last_error())
def read(address,n):
    buf=ctypes.create_string_buffer(n);actual=ctypes.c_size_t()
    if not kernel.ReadProcessMemory(h,address,buf,n,ctypes.byref(actual)): raise ctypes.WinError(ctypes.get_last_error())
    return buf.raw[:actual.value]
for index in range(3):
    addr=0x14084dc+index*16
    header=struct.unpack('<4I',read(addr,16))
    print('MAP',index,'header',*[hex(v) for v in header])
    pointer,count,_,_=header
    if pointer and 0<count<64:
        data=read(pointer,count*40)
        for action in range(min(count,8)):
            print(' action',action,'record',struct.unpack_from('<10I',data,action*40))
kernel.CloseHandle(h)
