// Included by padfix.cpp. All hooks are local to this executable's IAT.
#include <d3d9.h>
#include <objbase.h>
#include <intrin.h>
#include <unordered_map>
#include <unordered_set>
static void initializeAll();
static int sharedInput=1,windowMode=1,imeIsolation=1,comCompatibility=1,quickClose=1;
static int renderWidth=0,renderHeight=0;
static HWND gameWindow;
static WNDPROC gameWindowProc;
static RECT monitorBounds;
static int earlyHookCount;
static bool graphicsPatched=false;
static decltype(&CreateWindowExA) realCreateWindow;
static decltype(&SetWindowPos) realSetWindowPos;
static decltype(&SetWindowLongA) realSetWindowLong;
static decltype(&ClipCursor) realClipCursor;
static decltype(&MessageBoxA) realMessageBox;
static decltype(&CoInitializeEx) realCoInitialize;
static IDirect3D9* (WINAPI* realCreateD3D)(UINT);
static BOOL (WINAPI* disableIme)(DWORD);
static BOOL (WINAPI* associateIme)(HWND,HIMC,DWORD);
struct TableHook {void* release;void* method;size_t index;};
static std::mutex tableMutex;
static std::unordered_map<void*,TableHook> tables;
static std::unordered_set<void*> renderDevices;
static void* originalMethod(void* object,size_t index) {
 std::lock_guard<std::mutex> lock(tableMutex);auto i=tables.find(*(void***)object);
 if(i==tables.end())return nullptr;return index==2?i->second.release:i->second.method;
}
static ULONG WINAPI platformRelease(void* object) {
 auto fn=(ULONG(WINAPI*)(void*))originalMethod(object,2);if(!fn)return 0;
 ULONG refs=fn(object);
 if(!refs){std::lock_guard<std::mutex> lock(tableMutex);renderDevices.erase(object);}
 return refs;
}
static void hookMethod(void* object,size_t index,void* hook) {
 // The Windows implementation uses private vtable entries beyond IDirect3D9Ex.
 // Patch just the required slots in place; never truncate/copy its private table.
 std::lock_guard<std::mutex> lock(tableMutex);void** table=*(void***)object;if(tables.count(table))return;
 DWORD old;if(!VirtualProtect(table+2,(index-1)*sizeof(void*),PAGE_READWRITE,&old))return;
 tables[table]={table[2],table[index],index};
 InterlockedExchangePointer(table+2,(void*)platformRelease);InterlockedExchangePointer(table+index,hook);
 DWORD discard;VirtualProtect(table+2,(index-1)*sizeof(void*),old,&discard);
}
static void forcePresentation(D3DPRESENT_PARAMETERS& p) {
 if(!windowMode)return;
 p.Windowed=TRUE;p.FullScreen_RefreshRateInHz=0;
 p.BackBufferWidth=renderWidth;p.BackBufferHeight=renderHeight;
 if(p.PresentationInterval!=D3DPRESENT_INTERVAL_IMMEDIATE)p.PresentationInterval=D3DPRESENT_INTERVAL_ONE;
}
static HRESULT WINAPI platformReset(IDirect3DDevice9* d,D3DPRESENT_PARAMETERS* p) {
 if(!p)return D3DERR_INVALIDCALL;
 auto fn=(HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*))originalMethod(d,16);
 bool ours=false;{std::lock_guard<std::mutex> lock(tableMutex);ours=renderDevices.count(d)!=0;}
 if(ours)forcePresentation(*p);HRESULT hr=fn?fn(d,p):D3DERR_INVALIDCALL;
 logf("D3D Reset %ux%u windowed=%d hr=%08lx",p->BackBufferWidth,p->BackBufferHeight,p->Windowed,hr);return hr;
}
static HRESULT WINAPI platformCreateDevice(IDirect3D9* d,UINT adapter,D3DDEVTYPE type,HWND focus,DWORD flags,D3DPRESENT_PARAMETERS* p,IDirect3DDevice9** out) {
 if(!p||!out)return D3DERR_INVALIDCALL;
 auto fn=(HRESULT(WINAPI*)(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9**))originalMethod(d,16);
 bool ours=gameWindow&&(focus==gameWindow||p->hDeviceWindow==gameWindow);
 if(ours)forcePresentation(*p);HRESULT hr=fn?fn(d,adapter,type,focus,flags,p,out):D3DERR_INVALIDCALL;
 logf("D3D CreateDevice %ux%u windowed=%d hr=%08lx",p->BackBufferWidth,p->BackBufferHeight,p->Windowed,hr);
 if(SUCCEEDED(hr)&&*out&&ours){{std::lock_guard<std::mutex> lock(tableMutex);renderDevices.insert(*out);}hookMethod(*out,16,(void*)platformReset);IDirect3DSurface9* back=nullptr;
  if(SUCCEEDED((*out)->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back))){D3DSURFACE_DESC desc={};back->GetDesc(&desc);logf("actual backbuffer %ux%u",desc.Width,desc.Height);back->Release();}}
 return hr;
}
static IDirect3D9* WINAPI platformCreateD3D(UINT sdk) {
 initializeAll();IDirect3D9* d=realCreateD3D?realCreateD3D(sdk):nullptr;
 if(d&&windowMode)hookMethod(d,16,(void*)platformCreateDevice);return d;
}
static bool replaceCode(BYTE* location,const BYTE* expected,SIZE_T n,const BYTE* replacement,SIZE_T size) {
 if(memcmp(location,expected,n))return false;DWORD old=0;
 if(!VirtualProtect(location,size,PAGE_EXECUTE_READWRITE,&old))return false;
 memcpy(location,replacement,size);FlushInstructionCache(GetCurrentProcess(),location,size);
 DWORD discard;VirtualProtect(location,size,old,&discard);return true;
}
static bool constantGetter(BYTE* base,DWORD rva,int value) {
 const BYTE prefix[]={0x0f,0xb6,0x05};BYTE code[]={0xb8,0,0,0,0,0xc3};memcpy(code+1,&value,4);
 return replaceCode(base+rva,prefix,sizeof(prefix),code,sizeof(code));
}
static void platformConfigure() {
 auto opt=[](const char* key,int def){return (int)GetPrivateProfileIntA("platform",key,def,iniPath);};
 sharedInput=opt("shared_input",1);windowMode=std::clamp(opt("window_mode",1),0,1);
 imeIsolation=opt("disable_game_ime",1);comCompatibility=opt("com_apartment_compat",1);quickClose=opt("close_without_confirmation",1);
 if(windowMode){
  HMODULE user=GetModuleHandleA("user32.dll");auto dpi=(BOOL(WINAPI*)(HANDLE))GetProcAddress(user,"SetProcessDpiAwarenessContext");
  auto threadDpi=(HANDLE(WINAPI*)(HANDLE))GetProcAddress(user,"SetThreadDpiAwarenessContext");
  BOOL processOk=dpi?dpi((HANDLE)-4):FALSE;HANDLE oldContext=threadDpi?threadDpi((HANDLE)-4):nullptr;
  logf("DPI awareness process=%d thread_previous=%p",processOk,oldContext);
 }
 MONITORINFO monitor={sizeof(monitor)};GetMonitorInfoA(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&monitor);monitorBounds=monitor.rcMonitor;
 renderWidth=std::clamp(opt("render_width",0),0,8192);renderHeight=std::clamp(opt("render_height",0),0,8192);
 if(!renderWidth)renderWidth=monitorBounds.right-monitorBounds.left;if(!renderHeight)renderHeight=monitorBounds.bottom-monitorBounds.top;
 renderWidth=std::max(renderWidth,640);renderHeight=std::max(renderHeight,360);
 if(imeIsolation){HMODULE imm=systemDll("imm32.dll");if(imm){disableIme=(decltype(disableIme))GetProcAddress(imm,"ImmDisableIME");associateIme=(decltype(associateIme))GetProcAddress(imm,"ImmAssociateContextEx");}}
 logf("platform hooks=%d shared=%d borderless=%d render=%dx%d IME=%d COM=%d close=%d",earlyHookCount,sharedInput,windowMode,renderWidth,renderHeight,imeIsolation,comCompatibility,quickClose);
}
static void patchGraphics() {
 if(!windowMode||graphicsPatched||!nativeMenuMap)return;
 BYTE* base=(BYTE*)GetModuleHandleA(nullptr);
 // Verify all getters before modifying any of them.
 const DWORD getters[]={0x75d00,0x75d20,0x75d40,0x75d60};
 for(auto r:getters)if(memcmp(base+r,"\x0f\xb6\x05",3))return;
 const BYTE modeCheck[]={0x83,0xec,0x10,0x55,0x8b,0x6c,0x24,0x1c};
 if(memcmp(base+0x1cafc0,modeCheck,sizeof(modeCheck)))return;
 bool ok=constantGetter(base,0x75d00,renderWidth)&&constantGetter(base,0x75d20,renderHeight)
  &&constantGetter(base,0x75d40,renderHeight)&&constantGetter(base,0x75d60,1);
 // Windowed buffers need not be physical fullscreen display modes. Actual
 // allocation/capability errors still come from Direct3D CreateDevice/Reset.
 const BYTE allowWindowedSize[]={0xb8,1,0,0,0,0xc2,8,0};
 ok=ok&&replaceCode(base+0x1cafc0,modeCheck,sizeof(modeCheck),allowWindowedSize,sizeof(allowWindowedSize));
 if(ok){*(int*)(base+0x39fd74)=renderWidth;*(int*)(base+0x39fd78)=renderHeight;graphicsPatched=true;}
 logf("native render size getters %s %dx%d",ok?"patched":"FAILED",renderWidth,renderHeight);
}
static LRESULT CALLBACK platformWindowProc(HWND h,UINT message,WPARAM w,LPARAM l) {
 if(message==WM_CLOSE&&quickClose){logf("WM_CLOSE: normal destroy without confirmation");if(IsWindow(h))DestroyWindow(h);return 0;}
 if(message==WM_DEVICECHANGE){std::lock_guard<std::recursive_mutex> lock(mutex);nextSourceScan=0;logf("WM_DEVICECHANGE: source rescan scheduled");}
 if(message==WM_IME_SETCONTEXT&&imeIsolation)return DefWindowProcA(h,message,w,0);
 return CallWindowProcA(gameWindowProc,h,message,w,l);
}
static void logApartment(const char* stage) {
 auto getApartment=(HRESULT(WINAPI*)(int*,int*))GetProcAddress(GetModuleHandleA("ole32.dll"),"CoGetApartmentType");
 int type=-1,qualifier=-1;HRESULT hr=getApartment?getApartment(&type,&qualifier):E_NOTIMPL;
 logf("COM apartment %s hr=%08lx type=%d qualifier=%d HKL=%p",stage,hr,type,qualifier,GetKeyboardLayout(0));
}
static HWND WINAPI platformCreateWindow(DWORD ex,LPCSTR cls,LPCSTR title,DWORD style,int x,int y,int width,int height,HWND parent,HMENU menu,HINSTANCE inst,LPVOID param) {
 initializeAll();bool target=(ULONG_PTR)cls>65535&&!strcmp(cls,"Orochi_Z_PC");
 if(target){
  logApartment("before window");
  if(disableIme)logf("ImmDisableIME before main window result=%d",disableIme((DWORD)-1));
  patchGraphics();ex&=~WS_EX_TOPMOST;
  if(windowMode){ex&=~(WS_EX_CLIENTEDGE|WS_EX_WINDOWEDGE|WS_EX_DLGMODALFRAME);style=(style&~(WS_OVERLAPPEDWINDOW))|WS_POPUP;
   x=monitorBounds.left;y=monitorBounds.top;width=monitorBounds.right-x;height=monitorBounds.bottom-y;}
 }
 HWND h=realCreateWindow(ex,cls,title,style,x,y,width,height,parent,menu,inst,param);
 if(target&&h){gameWindow=h;gameWindowProc=(WNDPROC)SetWindowLongPtrA(h,GWLP_WNDPROC,(LONG_PTR)platformWindowProc);
  logApartment("after window");
  if(associateIme)associateIme(h,nullptr,0);
  if(windowMode)SetWindowPos(h,HWND_NOTOPMOST,x,y,width,height,SWP_FRAMECHANGED|SWP_NOACTIVATE);
  else SetWindowPos(h,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
  logf("main window hooked hwnd=%p bounds=%d,%d %dx%d",h,x,y,width,height);}
 return h;
}
static BOOL WINAPI platformSetWindowPos(HWND h,HWND after,int x,int y,int width,int height,UINT flags) {
 if(h==gameWindow){
  if(after==HWND_TOPMOST)after=HWND_NOTOPMOST;
  if(windowMode){x=monitorBounds.left;y=monitorBounds.top;width=monitorBounds.right-x;height=monitorBounds.bottom-y;
   flags&=~(SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER);after=HWND_NOTOPMOST;}
 }
 return realSetWindowPos(h,after,x,y,width,height,flags);
}
static LONG WINAPI platformSetWindowLong(HWND h,int index,LONG value) {
 if(h==gameWindow){if(index==GWL_EXSTYLE)value&=~WS_EX_TOPMOST;
  if(windowMode&&index==GWL_STYLE)value=(value&~WS_OVERLAPPEDWINDOW)|WS_POPUP;
  if(windowMode&&index==GWL_EXSTYLE)value&=~(WS_EX_CLIENTEDGE|WS_EX_WINDOWEDGE|WS_EX_DLGMODALFRAME);}
 return realSetWindowLong(h,index,value);
}
static BOOL WINAPI platformClipCursor(const RECT* rect){return realClipCursor(sharedInput?nullptr:rect);}
static int WINAPI platformMessageBox(HWND h,LPCSTR text,LPCSTR title,UINT flags) {
 initializeAll();char hex[161]={};if(text)for(size_t i=0;i<80&&text[i];i++)sprintf_s(hex+i*2,sizeof(hex)-i*2,"%02x",(unsigned char)text[i]);
 logf("MessageBox flags=%x caller=%p text_hex=%s",flags,_ReturnAddress(),hex);
 // This build's UI strings are CP950. Decode only its own static strings.
 BYTE* base=(BYTE*)GetModuleHandleA(nullptr);auto nt=(IMAGE_NT_HEADERS*)(base+((IMAGE_DOS_HEADER*)base)->e_lfanew);
 if((const BYTE*)text>=base&&(const BYTE*)text<base+nt->OptionalHeader.SizeOfImage){
  wchar_t wt[1024]={},wc[256]={};MultiByteToWideChar(950,0,text,-1,wt,1024);MultiByteToWideChar(950,0,title?title:"",-1,wc,256);
  return MessageBoxW(h,wt,wc,flags);
 }
 return realMessageBox(h,text,title,flags);
}
static HRESULT finishCoInitialize(LPVOID reserved,DWORD model,HRESULT hr,void* caller) {
 BYTE* base=(BYTE*)GetModuleHandleA(nullptr);
 if(comCompatibility&&hr==RPC_E_CHANGED_MODE&&model==COINIT_MULTITHREADED&&caller==base+0x1aed3a){
  HRESULT retry=realCoInitialize(reserved,COINIT_APARTMENTTHREADED);
  logf("COM existing STA retained with balanced reference hr=%08lx",retry);return retry;
 }
 return hr;
}
static HRESULT WINAPI platformCoInitialize(LPVOID reserved,DWORD model) {
 initializeAll();HRESULT hr=realCoInitialize(reserved,model);void* caller=_ReturnAddress();
 logf("CoInitializeEx model=%lu hr=%08lx caller=%p",model,hr,caller);
 return finishCoInitialize(reserved,model,hr,caller);
}
[[maybe_unused]] static void installEarlyHooks() {
 BYTE* base=(BYTE*)GetModuleHandleA(nullptr);
 auto nt=(IMAGE_NT_HEADERS*)(base+((IMAGE_DOS_HEADER*)base)->e_lfanew);
 if(nt->OptionalHeader.SizeOfImage<0x100850c||memcmp(base+0x1e3190,"\x8b\x44\x24\x04\xc1\xe0\x04\x05\xdc\x84\x40\x01\xc3",13))return;
 // This packed PE aliases OriginalFirstThunk and FirstThunk. The loader has
 // overwritten import names with function pointers; use the verified IAT RVAs.
 struct Entry{DWORD rva;void* hook;void** original;};
 Entry hooks[]={
  {0x2cc248,(void*)platformCreateWindow,(void**)&realCreateWindow},
  {0x2cc278,(void*)platformSetWindowPos,(void**)&realSetWindowPos},
  {0x2cc25c,(void*)platformSetWindowLong,(void**)&realSetWindowLong},
  {0x2cc258,(void*)platformClipCursor,(void**)&realClipCursor},
  {0x2cc290,(void*)platformMessageBox,(void**)&realMessageBox},
  {0x2cc2ec,(void*)platformCoInitialize,(void**)&realCoInitialize},
  {0x2cc2d0,(void*)platformCreateD3D,(void**)&realCreateD3D}};
 for(auto& hook:hooks){
  void** slot=(void**)(base+hook.rva);DWORD old;
  if(VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old)){
   *hook.original=*slot;InterlockedExchangePointer(slot,hook.hook);
   DWORD discard;VirtualProtect(slot,sizeof(void*),old,&discard);earlyHookCount++;
  }
 }
}
