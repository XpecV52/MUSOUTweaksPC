"""Launch target only when not running; read its own windows and dialog text."""
import ctypes,json,subprocess,time
from pathlib import Path
from ctypes import wintypes as w
u=ctypes.WinDLL('user32',use_last_error=True)
u.GetForegroundWindow.restype=w.HWND
u.GetWindowThreadProcessId.argtypes=[w.HWND,ctypes.POINTER(w.DWORD)];u.GetWindowThreadProcessId.restype=w.DWORD
u.GetKeyboardLayout.argtypes=[w.DWORD];u.GetKeyboardLayout.restype=ctypes.c_void_p
u.GetWindowTextW.argtypes=[w.HWND,w.LPWSTR,ctypes.c_int]
u.GetWindowTextA.argtypes=[w.HWND,ctypes.c_char_p,ctypes.c_int]
u.GetClassNameW.argtypes=[w.HWND,w.LPWSTR,ctypes.c_int]
u.IsWindowVisible.argtypes=[w.HWND]
CB=ctypes.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
u.EnumWindows.argtypes=[CB,w.LPARAM];u.EnumChildWindows.argtypes=[w.HWND,CB,w.LPARAM]
def text(hwnd,cls=False):
    buf=ctypes.create_unicode_buffer(4096)
    (u.GetClassNameW if cls else u.GetWindowTextW)(hwnd,buf,len(buf));return buf.value
def windows(pid):
    rows=[]
    @CB
    def cb(hwnd,_):
        p=w.DWORD();u.GetWindowThreadProcessId(hwnd,ctypes.byref(p))
        if p.value==pid and u.IsWindowVisible(hwnd):
            children=[]
            @CB
            def child(c,_):
                value=text(c)
                if value:
                    raw=ctypes.create_string_buffer(4096);u.GetWindowTextA(c,raw,len(raw))
                    children.append({'class':text(c,True),'text':value,'ansi_hex':raw.value.hex(),'cp950':raw.value.decode('cp950',errors='replace')})
                return True
            u.EnumChildWindows(hwnd,child,0)
            rows.append({'hwnd':int(hwnd),'class':text(hwnd,True),'text':text(hwnd),'children':children})
        return True
    u.EnumWindows(cb,0);return rows
active=u.GetWindowThreadProcessId(u.GetForegroundWindow(),None)
record={'foreground_hkl':hex(u.GetKeyboardLayout(active) or 0),'launcher_hkl':hex(u.GetKeyboardLayout(0) or 0)}
game=Path(r'E:\Games\Non-Steam\Musou Orochi Z\OROCHI_Z_TC.exe')
running=subprocess.run(['powershell','-NoProfile','-Command','(Get-Process -Name OROCHI_Z_TC -ErrorAction SilentlyContinue).Id'],capture_output=True,text=True).stdout.strip()
if running:
    pid=int(running.splitlines()[0]);record['existing']=True;proc=None
else:
    proc=subprocess.Popen([str(game)],cwd=str(game.parent));pid=proc.pid;record['existing']=False
record['pid']=pid;last=None;record['observations']=[]
for i in range(100):
    rows=windows(pid)
    if rows!=last:
        record['observations'].append({'elapsed':round(i*.2,1),'windows':rows})
        print(json.dumps(record['observations'][-1],ensure_ascii=True),flush=True);last=rows
    if proc and proc.poll() is not None:
        record['exit_code']=proc.returncode;break
    if any(r['class']=='#32770' for r in rows):break
    time.sleep(.2)
Path(__file__).with_name('startup_observation.json').write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in record.items() if k!='observations'},ensure_ascii=False))
