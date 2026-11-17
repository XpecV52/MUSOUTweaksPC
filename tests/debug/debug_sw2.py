"""Bounded debugger observation of a newly launched game; never attach elsewhere."""
import ctypes,json,time
from ctypes import wintypes as w
from pathlib import Path
k=ctypes.WinDLL('kernel32',use_last_error=True)
class Startup(ctypes.Structure):
    _fields_=[('cb',w.DWORD),('reserved',w.LPWSTR),('desktop',w.LPWSTR),('title',w.LPWSTR),('x',w.DWORD),('y',w.DWORD),('xs',w.DWORD),('ys',w.DWORD),('xc',w.DWORD),('yc',w.DWORD),('fill',w.DWORD),('flags',w.DWORD),('show',w.WORD),('reserved2',w.WORD),('reservedPtr',ctypes.c_void_p),('stdin',w.HANDLE),('stdout',w.HANDLE),('stderr',w.HANDLE)]
class Process(ctypes.Structure):
    _fields_=[('process',w.HANDLE),('thread',w.HANDLE),('pid',w.DWORD),('tid',w.DWORD)]
class Event(ctypes.Structure):
    _fields_=[('code',w.DWORD),('pid',w.DWORD),('tid',w.DWORD),('padding',w.DWORD),('data',ctypes.c_ubyte*160)]
class ExceptionRecord(ctypes.Structure):
    _fields_=[('code',w.DWORD),('flags',w.DWORD),('record',ctypes.c_void_p),('address',ctypes.c_void_p),('count',w.DWORD),('info',ctypes.c_size_t*15)]
k.CreateProcessW.argtypes=[w.LPCWSTR,w.LPWSTR,ctypes.c_void_p,ctypes.c_void_p,w.BOOL,w.DWORD,ctypes.c_void_p,w.LPCWSTR,ctypes.POINTER(Startup),ctypes.POINTER(Process)]
k.WaitForDebugEvent.argtypes=[ctypes.POINTER(Event),w.DWORD];k.ContinueDebugEvent.argtypes=[w.DWORD,w.DWORD,w.DWORD]
k.ReadProcessMemory.argtypes=[w.HANDLE,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t)]
k.GetFinalPathNameByHandleW.argtypes=[w.HANDLE,w.LPWSTR,w.DWORD,w.DWORD];k.CloseHandle.argtypes=[w.HANDLE]
k.DebugActiveProcessStop.argtypes=[w.DWORD]
game=Path(r'E:\Games\Non-Steam\Sengoku Musou 2\SW2.exe')
start=Startup();start.cb=ctypes.sizeof(start);proc=Process()
k.DebugSetProcessKillOnExit(False)
if not k.CreateProcessW(str(game),None,None,None,False,2,None,str(game.parent),ctypes.byref(start),ctypes.byref(proc)):raise ctypes.WinError(ctypes.get_last_error())
rows=[];modules={};deadline=time.monotonic()+25;exited=False
def log(row):rows.append(row);print(json.dumps(row,ensure_ascii=True),flush=True)
while time.monotonic()<deadline:
    event=Event()
    if not k.WaitForDebugEvent(ctypes.byref(event),100):continue
    raw=bytes(event.data);status=0x10002
    if event.code in (3,6):
        file=int.from_bytes(raw[:8],'little');base=int.from_bytes(raw[24:32] if event.code==3 else raw[8:16],'little')
        path=ctypes.create_unicode_buffer(1024)
        if file:k.GetFinalPathNameByHandleW(file,path,1024,0);k.CloseHandle(file)
        modules[base]=path.value
    if event.code==1:
        ex=ExceptionRecord.from_buffer_copy(raw[:ctypes.sizeof(ExceptionRecord)])
        addr=ex.address or 0;previous=[b for b in modules if b<=addr];base=max(previous) if previous else 0
        log({'exception':hex(ex.code),'address':hex(addr),'module':modules.get(base),'offset':hex(addr-base),'params':[hex(ex.info[i]) for i in range(min(ex.count,15))]})
        if ex.code not in (0x80000003,0x4000001f):status=0x80010001
    if event.code==5:log({'exit_code':hex(int.from_bytes(raw[:4],'little'))});exited=True
    k.ContinueDebugEvent(event.pid,event.tid,status)
    if exited:break
if not exited:k.DebugActiveProcessStop(proc.pid);log({'detached_running_pid':proc.pid})
k.CloseHandle(proc.thread);k.CloseHandle(proc.process)
Path(__file__).with_name('debug_sw2.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')

