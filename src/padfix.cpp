#define DIRECTINPUT_VERSION 0x0800
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dinput.h>
#include <Xinput.h>
#include <algorithm>
#include <vector>
#include <deque>
#include <mutex>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <imm.h>

// Xbox -> one application-local DirectInput controller. No system input injection.
static HMODULE selfModule;
static char iniPath[MAX_PATH], logPath[MAX_PATH];
static int enabled=1, foregroundOnly=1, menuNavigation=1, dpadPov=0, slotConfig=-1;
static int dpadAsLeftStick=0;
static int leftDeadzone=20, rightThreshold=55, triggerThreshold=40, neutralMs=250;
static bool logging=true;
static std::recursive_mutex mutex;
static DWORD (WINAPI *getXState)(DWORD,XINPUT_STATE*);
static int backend=0,selectedBackend=0; // 0 auto, 1 XInput, 2 native DS4
static int ds4RightX=2,ds4RightY=5;
static HRESULT (WINAPI* createSystemInput)(HINSTANCE,DWORD,REFIID,void**,LPUNKNOWN);
static IDirectInput8A* sourceInput;
static IDirectInputDevice8A* sourcePad;
static HWND sourceWindow;
static ULONGLONG nextSourceScan;
static const WORD buttonMasks[10]={XINPUT_GAMEPAD_A,XINPUT_GAMEPAD_B,XINPUT_GAMEPAD_X,
 XINPUT_GAMEPAD_Y,XINPUT_GAMEPAD_LEFT_SHOULDER,XINPUT_GAMEPAD_RIGHT_SHOULDER,
 XINPUT_GAMEPAD_BACK,XINPUT_GAMEPAD_START,XINPUT_GAMEPAD_LEFT_THUMB,XINPUT_GAMEPAD_RIGHT_THUMB};
static void ds4ToXbox(const DIJOYSTATE2& raw,XINPUT_STATE& out) {
 out={}; auto& g=out.Gamepad;
 auto bounded=[](LONG v)->SHORT{return (SHORT)std::clamp(v,-32767L,32767L);};
 const LONG axes[]={raw.lX,raw.lY,raw.lZ,raw.lRx,raw.lRy,raw.lRz};
 g.sThumbLX=bounded(raw.lX);g.sThumbLY=bounded(-raw.lY);
 g.sThumbRX=bounded(axes[ds4RightX]);g.sThumbRY=bounded(-axes[ds4RightY]);
 const int sourceButtons[]={1,2,0,3,4,5,8,9,10,11};
 for(int k=0;k<10;k++)if(raw.rgbButtons[sourceButtons[k]]&0x80)g.wButtons|=buttonMasks[k];
 g.bLeftTrigger=(raw.rgbButtons[6]&0x80)?255:0;g.bRightTrigger=(raw.rgbButtons[7]&0x80)?255:0;
 DWORD pov=raw.rgdwPOV[0];
 if(LOWORD(pov)!=0xffff&&pov<36000) {
  int octant=((pov+2250)/4500)%8;
  const WORD hats[]={1,1|8,8,8|2,2,2|4,4,4|1};g.wButtons|=hats[octant];
 }
}
static const GUID padGuid={0x8f8bf701,0x7253,0x42ae,{0x95,0xf3,0x83,0x2a,0x6d,0x88,0x6e,0x10}};
struct NativeBinding { int device; BYTE axis,padding[3]; int sign,object; };
struct NativeAction { DWORD flags,combine; NativeBinding binding[2]; };
struct NativeMap { NativeAction* actions; DWORD count; BYTE useDevice,padding[3]; DWORD device; };
static_assert(sizeof(NativeBinding)==16&&sizeof(NativeAction)==40&&sizeof(NativeMap)==16);
static NativeMap* nativeMenuMap;
static bool sameBinding(const NativeBinding& a,const NativeBinding& b) {
 return a.device==b.device&&a.axis==b.axis&&a.sign==b.sign&&a.object==b.object;
}
// The menu map has keyboard arrows in slot 0 and an unused slot 1. Player
// maps are separate allocations: never synthesize keyboard state to navigate.
static bool patchMenuMap(NativeMap& map,bool& changed) {
 changed=false;
 if(!map.actions||map.count!=16)return false;
 const int arrows[]={DIK_UP,DIK_DOWN,DIK_LEFT,DIK_RIGHT};
 for(int i=0;i<4;i++) {
  const auto& a=map.actions[i];NativeBinding dpad={};dpad.device=0;dpad.object=12+i;
  if(a.binding[0].device!=8||a.binding[0].object!=arrows[i])return false;
  if(a.binding[1].device!=10&&!sameBinding(a.binding[1],dpad))return false;
 }
 for(int i=0;i<4;i++) {
  NativeBinding dpad={};dpad.device=0;dpad.object=12+i;
  if(!sameBinding(map.actions[i].binding[1],dpad)){map.actions[i].binding[1]=dpad;changed=true;}
 }
 return true;
}
static bool writableMemory(void* ptr,SIZE_T bytes) {
 MEMORY_BASIC_INFORMATION m={};if(!VirtualQuery(ptr,&m,sizeof(m)))return false;
 if(m.State!=MEM_COMMIT||m.Protect&(PAGE_GUARD|PAGE_NOACCESS))return false;
 if(!(m.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
 return (BYTE*)ptr>=(BYTE*)m.BaseAddress&&bytes<=m.RegionSize-((BYTE*)ptr-(BYTE*)m.BaseAddress);
}
static void installMenuDpad();
static void logf(const char* fmt,...) {
 if(!logging||!logPath[0])return;
 std::lock_guard<std::recursive_mutex> lock(mutex);
 FILE* f=nullptr; fopen_s(&f,logPath,"a"); if(!f)return;
 fprintf(f,"[%lu] ",GetTickCount()); va_list a;va_start(a,fmt);vfprintf(f,fmt,a);va_end(a);
 fputc('\n',f);fclose(f);
}
static HMODULE systemDll(const char* name) {
 char path[MAX_PATH]; GetSystemDirectoryA(path,MAX_PATH);strcat_s(path,"\\");strcat_s(path,name);
 return LoadLibraryA(path);
}
#include "platform.inc"
static BOOL CALLBACK ds4Candidate(const DIDEVICEINSTANCEA* i,void*) {
 logf("source candidate: %s VIDPID=%08lx",i->tszInstanceName,i->guidProduct.Data1);
 WORD vid=LOWORD(i->guidProduct.Data1),pid=HIWORD(i->guidProduct.Data1);
 if(vid!=0x054c||(pid!=0x05c4&&pid!=0x09cc))return DIENUM_CONTINUE;
 IDirectInputDevice8A* d=nullptr;HRESULT hr=sourceInput->CreateDevice(i->guidInstance,&d,nullptr);
 if(FAILED(hr)||!d)return DIENUM_CONTINUE;
 hr=d->SetDataFormat(&c_dfDIJoystick2);
 if(SUCCEEDED(hr))hr=d->SetCooperativeLevel(sourceWindow,DISCL_BACKGROUND|DISCL_NONEXCLUSIVE);
 DIPROPRANGE range={{sizeof(range),sizeof(DIPROPHEADER),0,DIPH_DEVICE},-32767,32767};
 if(SUCCEEDED(hr))hr=d->SetProperty(DIPROP_RANGE,&range.diph);
 if(SUCCEEDED(hr))hr=d->Acquire();
 if(FAILED(hr)){logf("DS4 source setup failed %08lx",hr);d->Release();return DIENUM_CONTINUE;}
 sourcePad=d;logf("DS4 native source selected: %s",i->tszInstanceName);return DIENUM_STOP;
}
static bool readDs4(XINPUT_STATE& s) {
 if(!sourcePad&&GetTickCount64()>=nextSourceScan) {
  nextSourceScan=GetTickCount64()+2000;
  if(!sourceInput&&createSystemInput)createSystemInput(GetModuleHandleA(nullptr),0x800,IID_IDirectInput8A,(void**)&sourceInput,nullptr);
  if(!sourceWindow)sourceWindow=CreateWindowExA(0,"STATIC","MUSOUTweaksPC input",0,0,0,1,1,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);
  if(sourceInput&&sourceWindow)sourceInput->EnumDevices(DI8DEVCLASS_GAMECTRL,ds4Candidate,nullptr,DIEDFL_ATTACHEDONLY);
 }
 if(!sourcePad)return false;
 sourcePad->Poll();DIJOYSTATE2 raw={};HRESULT hr=sourcePad->GetDeviceState(sizeof(raw),&raw);
 if(FAILED(hr)){sourcePad->Acquire();sourcePad->Poll();hr=sourcePad->GetDeviceState(sizeof(raw),&raw);}
 if(FAILED(hr)){sourcePad->Release();sourcePad=nullptr;return false;}
 ds4ToXbox(raw,s);return true;
}
static const char* configFileNameForExecutable(const char* executable) {
 if(!_stricmp(executable,"ShinSangokumusou3.exe"))return "SSM3_Tweaks.ini";
 if(!_stricmp(executable,"SM4_WIN.exe")||!_stricmp(executable,"SS4.exe")||!_stricmp(executable,"SS4_Plus.exe")||!_stricmp(executable,"SS4_Super.exe")||!_stricmp(executable,"SS4_Ultima.exe"))return "SSM4_Tweaks.ini";
 if(!_stricmp(executable,"SM6TC.exe")||!_stricmp(executable,"SM6EN.exe")||!_stricmp(executable,"SM6JP.exe"))return "SSM6_Tweaks.ini";
 if(!_stricmp(executable,"Launch.exe"))return "SSM7_Tweaks.ini";
 if(!_stricmp(executable,"SW2.exe"))return "SM2_Tweaks.ini";
 return "OrochiZPadX.ini";
}
static void init() {
 GetModuleFileNameA(selfModule,iniPath,MAX_PATH);
 char* slash=strrchr(iniPath,'\\'); if(slash)slash[1]=0;
 char executablePath[MAX_PATH]={};GetModuleFileNameA(nullptr,executablePath,MAX_PATH);const char* executable=strrchr(executablePath,'\\');executable=executable?executable+1:executablePath;
 strcpy_s(gameExeName,executable);
 strcpy_s(logPath,iniPath);strcat_s(logPath,"MUSOUTweaksPC.log");strcat_s(iniPath,configFileNameForExecutable(executable));
#ifdef PADFIX_PLATFORM_ONLY
 enabled=0;logging=GetPrivateProfileIntA("platform","log",1,iniPath)!=0;
 platformConfigure();
 logf("MUSOUTweaksPC 2.18 platform-only x86 config=%s",iniPath);
 return;
#else
 auto opt=[](const char* key,int def){return (int)GetPrivateProfileIntA("padfix",key,def,iniPath);};
 enabled=opt("enable",1);logging=opt("log",1)!=0;foregroundOnly=opt("foreground_only",1);
 menuNavigation=opt("dpad_menu_navigation",1);dpadPov=opt("dpad_pov",0);slotConfig=std::clamp(opt("xinput_slot",-1),-1,3);
 leftDeadzone=std::clamp(opt("left_deadzone",20),0,90);
 rightThreshold=std::clamp(opt("right_threshold",55),10,95);
 triggerThreshold=std::clamp(opt("trigger_threshold",40),1,254);
 neutralMs=std::clamp(opt("neutral_ms",250),0,5000);
 backend=std::clamp(opt("backend",0),0,2);
 int dpadAsLeftStickSetting=std::clamp(opt("dpad_as_left_stick",-1),-1,1);
 ds4RightX=std::clamp(opt("ds4_right_x",2),0,5);ds4RightY=std::clamp(opt("ds4_right_y",5),0,5);
 HMODULE x=systemDll("xinput1_4.dll"); if(!x)x=systemDll("xinput9_1_0.dll");
 if(x)getXState=(decltype(getXState))GetProcAddress(x,"XInputGetState");
 HMODULE d=systemDll("dinput8.dll");if(d)createSystemInput=(decltype(createSystemInput))GetProcAddress(d,"DirectInput8Create");
 logf("MUSOUTweaksPC 2.18 x86 enable=%d backend=%d slot=%d menu_navigation=%d XInput=%d config=%s",enabled,backend,slotConfig,menuNavigation,getXState!=nullptr,iniPath);
 // Only the inspected build is supported for native menu table integration.
 // Match two independent code fingerprints before resolving the table RVA.
 BYTE* base=(BYTE*)GetModuleHandleA(nullptr);
 auto nt=(IMAGE_NT_HEADERS*)(base+((IMAGE_DOS_HEADER*)base)->e_lfanew);
 const BYTE lookup[]={0x8b,0x44,0x24,0x04,0xc1,0xe0,0x04,0x05,0xdc,0x84,0x40,0x01,0xc3};
 const BYTE menuInit[]={0x83,0xec,0x28,0x6a,0x10,0xb9,0xfc,0x84,0x40,0x01};
 if(nt->OptionalHeader.SizeOfImage>=0x100850c&&
    !memcmp(base+0x1e3190,lookup,sizeof(lookup))&&
    !memcmp(base+0x1e2f30,menuInit,sizeof(menuInit))) {
  nativeMenuMap=(NativeMap*)(base+0x10084fc);
  logf("Orochi Z native menu map signature matched");
 }
 char exe[MAX_PATH]={};GetModuleFileNameA(nullptr,exe,MAX_PATH);const char* name=strrchr(exe,'\\');name=name?name+1:exe;
 const BYTE ss4MenuInit[]={0x83,0xec,0x28,0x6a,0x10,0xb9,0xc4,0x07,0x6e,0x00};
 if(!nativeMenuMap&&(!_stricmp(name,"SM4_WIN.exe")||!_stricmp(name,"SS4.exe")||!_stricmp(name,"SS4_Plus.exe")||!_stricmp(name,"SS4_Super.exe")||!_stricmp(name,"SS4_Ultima.exe"))&&
    nt->OptionalHeader.SizeOfImage>0x2e07c4+sizeof(NativeMap)&&!memcmp(base+0x1dd40,ss4MenuInit,sizeof(ss4MenuInit))) {
  nativeMenuMap=(NativeMap*)(base+0x2e07c4);logf("SS4 native menu map signature matched");
 }
 if(!nativeMenuMap)logf("native menu map unavailable for this EXE; D-pad POV remains available by configuration");
 platformConfigure();
 dpadAsLeftStick=dpadAsLeftStickSetting<0?!_stricmp(gameExeName,"ShinSangokumusou3.exe"):dpadAsLeftStickSetting;
 logf("D-pad to left-stick bridge=%d",dpadAsLeftStick);
#endif
}
static void initializeAll(){static std::once_flag once;std::call_once(once,[]{init();});}
static void installMenuDpad() {
 if(!menuNavigation||!nativeMenuMap)return;
 if(!writableMemory(nativeMenuMap,sizeof(*nativeMenuMap))||nativeMenuMap->count!=16)return;
 if(!writableMemory(nativeMenuMap->actions,4*sizeof(NativeAction)))return;
 bool changed=false;
 if(patchMenuMap(*nativeMenuMap,changed)&&changed)logf("native menu D-pad installed: map=2 actions=0..3 buttons=13..16; player maps untouched");
}
struct PadState { LONG x=0,y=0; BYTE buttons[20]={}; WORD dpad=0; };
static int selectedSlot=-1;
static bool armed=false, wasConnected=false, wasForeground=false, neutralPending=false;
static int loggedForeground=-1;
static ULONGLONG neutralSince;
static ULONGLONG lastConnection;
static WORD loggedDpad=0xffff;
static bool foreground() {
 if(!foregroundOnly)return true;DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
 return pid==GetCurrentProcessId();
}
static PadState sample() {
 std::lock_guard<std::recursive_mutex> lock(mutex);
 PadState out; XINPUT_STATE s={}; bool connected=false;
 if(getXState&&backend!=2&&selectedBackend!=2) {
  if(slotConfig>=0){selectedSlot=slotConfig;connected=getXState(selectedSlot,&s)==ERROR_SUCCESS;}
  else if(selectedSlot>=0)connected=getXState(selectedSlot,&s)==ERROR_SUCCESS;
  else for(int i=0;i<4;i++)if(getXState(i,&s)==ERROR_SUCCESS){selectedSlot=i;connected=true;break;}
 }
 if(connected)selectedBackend=1;
 if(!connected&&backend!=1&&selectedBackend!=1&&readDs4(s)){connected=true;selectedBackend=2;}
 if(connected)lastConnection=GetTickCount64();
 // Preserve the selected source through short dropouts; rescan after unplug.
 if(!connected&&selectedBackend&&GetTickCount64()-lastConnection>=1500){
  if(backend==0)selectedBackend=0;
  if(slotConfig<0)selectedSlot=-1;
 }
 bool fg=foreground();
 if(loggedForeground!=(int)fg){loggedForeground=(int)fg;logf("controller game_foreground=%d",fg);}
 if(connected!=wasConnected){logf("controller %s backend=%s slot=%d",connected?"connected":"disconnected",selectedBackend==2?"DS4":"XInput",selectedSlot);wasConnected=connected;}
 if(!connected||!fg||!wasForeground){armed=false;neutralPending=false;}
 wasForeground=fg;
 if(!connected||!fg)return out;
 auto& g=s.Gamepad;
 WORD dpad=g.wButtons&15;if(dpad!=loggedDpad){loggedDpad=dpad;logf("controller D-pad bits=%x backend=%s",dpad,selectedBackend==2?"DS4":"XInput");}
 int dz=leftDeadzone*32767/100, rt=rightThreshold*32767/100;
 bool neutral=!g.wButtons&&g.bLeftTrigger<triggerThreshold&&g.bRightTrigger<triggerThreshold
  &&abs((int)g.sThumbLX)<=dz&&abs((int)g.sThumbLY)<=dz
  &&abs((int)g.sThumbRX)<rt&&abs((int)g.sThumbRY)<rt;
 if(!armed) {
  if(!neutral){neutralPending=false;return out;}
  if(!neutralPending){neutralSince=GetTickCount64();neutralPending=true;}
  if(GetTickCount64()-neutralSince<(ULONGLONG)neutralMs)return out;
  armed=true;logf("controller armed after neutral");
 }
 auto axis=[dz](int v)->LONG {if(abs(v)<=dz)return 0;return std::clamp(v,-32767,32767)*255/32767;};
 out.x=axis(g.sThumbLX);out.y=-axis(g.sThumbLY);
 for(int i=0;i<10;i++)out.buttons[i]=(g.wButtons&buttonMasks[i])?0x80:0;
 out.buttons[10]=g.bLeftTrigger>=triggerThreshold?0x80:0;
 out.buttons[11]=g.bRightTrigger>=triggerThreshold?0x80:0;
 out.dpad=g.wButtons&15;
 static WORD loggedForwardedDpad=0xffff;
 if(out.dpad!=loggedForwardedDpad){loggedForwardedDpad=out.dpad;logf("virtual DirectInput forwarded D-pad bits=%x left=%ld,%ld",out.dpad,out.x,out.y);}
 out.buttons[12]=(out.dpad&XINPUT_GAMEPAD_DPAD_UP)?0x80:0;
 out.buttons[13]=(out.dpad&XINPUT_GAMEPAD_DPAD_DOWN)?0x80:0;
 out.buttons[14]=(out.dpad&XINPUT_GAMEPAD_DPAD_LEFT)?0x80:0;
 out.buttons[15]=(out.dpad&XINPUT_GAMEPAD_DPAD_RIGHT)?0x80:0;
 out.buttons[16]=g.sThumbRY>rt?0x80:0;out.buttons[17]=g.sThumbRY<-rt?0x80:0;
 out.buttons[18]=g.sThumbRX<-rt?0x80:0;out.buttons[19]=g.sThumbRX>rt?0x80:0;
 // The SM3 PC build does not consume the POV/button D-pad through its
 // default native menu map. Mirror digital directions to the left stick for
 // that profile, while preserving independent physical left-stick input.
 if(dpadAsLeftStick){
  if(out.dpad&XINPUT_GAMEPAD_DPAD_LEFT)out.x=-255;
  else if(out.dpad&XINPUT_GAMEPAD_DPAD_RIGHT)out.x=255;
  // SM3 reads the virtual DirectInput Y axis opposite to this adapter's prior
  // assumption: positive moves the menu down, so invert the D-pad bridge.
  if(out.dpad&XINPUT_GAMEPAD_DPAD_UP)out.y=-255;
  else if(out.dpad&XINPUT_GAMEPAD_DPAD_DOWN)out.y=255;
 }
 return out;
}
static DIDEVICEINSTANCEA instance() {
 DIDEVICEINSTANCEA i={};i.dwSize=sizeof(i);i.guidInstance=i.guidProduct=padGuid;
 i.dwDevType=DI8DEVTYPE_GAMEPAD|(DI8DEVTYPEGAMEPAD_STANDARD<<8);
 strcpy_s(i.tszInstanceName,"MUSOUTweaksPC Gamepad");strcpy_s(i.tszProductName,"MUSOUTweaksPC Gamepad");return i;
}
struct Field { DWORD offset,type;int index; };
class Device final:public IDirectInputDevice8A {
 LONG refs=1; IDirectInputDevice8A* real=nullptr; bool keyboard=false,acquired=false;
 std::vector<Field> fields; DWORD dataSize=0,bufferSize=0,sequence=0;
 LONG minimum[2]={0,0},maximum[2]={65535,65535};
 std::vector<BYTE> previous; std::deque<DIDEVICEOBJECTDATA> events;
 DWORD resolveAxis(const DIPROPHEADER* h) {
  if(h->dwHow==DIPH_DEVICE)return 2;
  if(h->dwHow==DIPH_BYID)return DIDFT_GETINSTANCE(h->dwObj);
  if(h->dwHow==DIPH_BYOFFSET)for(auto& f:fields)if(f.offset==h->dwObj&&(f.type&DIDFT_AXIS))return f.index;
  return 99;
 }
 void pushEvent(DWORD ofs,DWORD val) {
  if(!bufferSize)return; DIDEVICEOBJECTDATA e={};e.dwOfs=ofs;e.dwData=val;
  e.dwTimeStamp=GetTickCount();e.dwSequence=sequence++;
  if(events.size()>=bufferSize)events.pop_front();events.push_back(e);
 }
 HRESULT render(DWORD n,void* ptr) {
  if(!ptr||n!=dataSize)return DIERR_INVALIDPARAM;
  if(!acquired)return DIERR_NOTACQUIRED;
  installMenuDpad();
  auto s=sample();memset(ptr,0,n);
  for(auto& f:fields) {
   BYTE* dst=(BYTE*)ptr+f.offset;
   if(f.type&DIDFT_BUTTON){*dst=f.index<20?s.buttons[f.index]:0;}
   else if(f.type&DIDFT_POV){
    DWORD v=0xffffffff;const WORD u=s.dpad;
    if(u&XINPUT_GAMEPAD_DPAD_UP){v=(u&XINPUT_GAMEPAD_DPAD_RIGHT)?4500:(u&XINPUT_GAMEPAD_DPAD_LEFT)?31500:0;}
    else if(u&XINPUT_GAMEPAD_DPAD_DOWN){v=(u&XINPUT_GAMEPAD_DPAD_RIGHT)?13500:(u&XINPUT_GAMEPAD_DPAD_LEFT)?22500:18000;}
    else if(u&XINPUT_GAMEPAD_DPAD_RIGHT)v=9000;else if(u&XINPUT_GAMEPAD_DPAD_LEFT)v=27000;
    if(!dpadPov)v=0xffffffff;memcpy(dst,&v,4);
   }
   else if(f.type&DIDFT_AXIS) {
    int i=f.index;LONG v=0;
    if(i<2){LONG a=i?s.y:s.x;v=(LONG)((long long)minimum[i]+((long long)a+255)*((long long)maximum[i]-minimum[i])/510);}
    memcpy(dst,&v,4);
   }
  }
  return DI_OK;
 }
 DIDEVICEOBJECTINSTANCEA object(int k) {
  DIDEVICEOBJECTINSTANCEA o={};o.dwSize=sizeof(o);
  if(k<2){o.guidType=k?GUID_YAxis:GUID_XAxis;o.dwOfs=k*4;o.dwType=DIDFT_ABSAXIS|DIDFT_MAKEINSTANCE(k);sprintf_s(o.tszName,"Left stick %c",k?'Y':'X');}
  else if(k==2&&dpadPov){o.guidType=GUID_POV;o.dwOfs=DIJOFS_POV(0);o.dwType=DIDFT_POV|DIDFT_MAKEINSTANCE(0);strcpy_s(o.tszName,"D-pad POV");}
  else{int b=k-(dpadPov?3:2);o.guidType=GUID_Button;o.dwOfs=DIJOFS_BUTTON(b);o.dwType=DIDFT_PSHBUTTON|DIDFT_MAKEINSTANCE(b);sprintf_s(o.tszName,"Button %d",b+1);}
  return o;
 }
public:
 explicit Device(IDirectInputDevice8A* r=nullptr,bool kb=false):real(r),keyboard(kb){}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p) override {if(!p)return E_POINTER;*p=nullptr;if(id==IID_IUnknown||id==IID_IDirectInputDevice8A){*p=this;AddRef();return S_OK;}return E_NOINTERFACE;}
 ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs);}
 ULONG STDMETHODCALLTYPE Release() override{ULONG r=InterlockedDecrement(&refs);if(!r){if(real)real->Release();delete this;}return r;}
 HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS c) override {
  if(real)return real->GetCapabilities(c);if(!c||c->dwSize!=sizeof(*c))return DIERR_INVALIDPARAM;
  DWORD size=c->dwSize;memset(c,0,sizeof(*c));c->dwSize=size;c->dwFlags=DIDC_ATTACHED|DIDC_EMULATED;
  c->dwDevType=instance().dwDevType;c->dwAxes=2;c->dwButtons=20;c->dwPOVs=dpadPov?1:0;return DI_OK;
 }
 HRESULT STDMETHODCALLTYPE EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKA cb,LPVOID ctx,DWORD flags) override {
  if(real)return real->EnumObjects(cb,ctx,flags);if(!cb)return DIERR_INVALIDPARAM;
  for(int i=0;i<(dpadPov?23:22);i++){auto o=object(i);if(flags==DIDFT_ALL||DIDFT_GETTYPE(flags&o.dwType))if(cb(&o,ctx)==DIENUM_STOP)break;}return DI_OK;
 }
 HRESULT STDMETHODCALLTYPE GetProperty(REFGUID prop,LPDIPROPHEADER h) override {
  if(real)return real->GetProperty(prop,h);if(!h)return DIERR_INVALIDPARAM;
  if(&prop==&DIPROP_RANGE){if(h->dwSize!=sizeof(DIPROPRANGE))return DIERR_INVALIDPARAM;DWORD a=resolveAxis(h);if(a>2)return DIERR_OBJECTNOTFOUND;auto p=(DIPROPRANGE*)h;p->lMin=minimum[a==2?0:a];p->lMax=maximum[a==2?0:a];return DI_OK;}
  if(&prop==&DIPROP_BUFFERSIZE||&prop==&DIPROP_DEADZONE||&prop==&DIPROP_SATURATION||&prop==&DIPROP_AXISMODE){if(h->dwSize!=sizeof(DIPROPDWORD))return DIERR_INVALIDPARAM;((DIPROPDWORD*)h)->dwData=&prop==&DIPROP_BUFFERSIZE?bufferSize:(&prop==&DIPROP_AXISMODE?DIPROPAXISMODE_ABS:0);return DI_OK;}
  return DIERR_UNSUPPORTED;
 }
 HRESULT STDMETHODCALLTYPE SetProperty(REFGUID prop,LPCDIPROPHEADER h) override {
  if(real){HRESULT hr=real->SetProperty(prop,h);if(SUCCEEDED(hr)&&keyboard&&&prop==&DIPROP_BUFFERSIZE&&h&&h->dwSize==sizeof(DIPROPDWORD))bufferSize=((DIPROPDWORD*)h)->dwData;return hr;}
  if(!h)return DIERR_INVALIDPARAM;
  if(&prop==&DIPROP_RANGE){if(h->dwSize!=sizeof(DIPROPRANGE))return DIERR_INVALIDPARAM;auto p=(const DIPROPRANGE*)h;if(p->lMin>=p->lMax)return DIERR_INVALIDPARAM;DWORD a=resolveAxis(h);if(a>2)return DIERR_OBJECTNOTFOUND;for(int k=0;k<2;k++)if(a==2||a==(DWORD)k){minimum[k]=p->lMin;maximum[k]=p->lMax;}return DI_OK;}
  if(&prop==&DIPROP_BUFFERSIZE){if(h->dwSize!=sizeof(DIPROPDWORD))return DIERR_INVALIDPARAM;bufferSize=std::min(((const DIPROPDWORD*)h)->dwData,1024UL);events.clear();previous.clear();return DI_OK;}
  if(&prop==&DIPROP_AXISMODE){if(h->dwSize!=sizeof(DIPROPDWORD))return DIERR_INVALIDPARAM;return ((const DIPROPDWORD*)h)->dwData==DIPROPAXISMODE_ABS?DI_OK:DIERR_UNSUPPORTED;}
  if(&prop==&DIPROP_DEADZONE||&prop==&DIPROP_SATURATION)return DI_OK;
  return DIERR_UNSUPPORTED;
 }
 HRESULT STDMETHODCALLTYPE Acquire() override {if(real)return real->Acquire();acquired=true;return DI_OK;}
 HRESULT STDMETHODCALLTYPE Unacquire() override {if(real)return real->Unacquire();acquired=false;events.clear();previous.clear();return DI_OK;}
 HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD n,LPVOID p) override {
  if(!real)return render(n,p);
  return real->GetDeviceState(n,p);
 }
 HRESULT STDMETHODCALLTYPE GetDeviceData(DWORD stride,LPDIDEVICEOBJECTDATA ptr,LPDWORD count,DWORD flags) override {
  if(!count||stride!=sizeof(DIDEVICEOBJECTDATA)||flags&~DIGDD_PEEK)return DIERR_INVALIDPARAM;
  DWORD capacity=*count;*count=0;
  if(real){*count=capacity;return real->GetDeviceData(stride,ptr,count,flags);}
  if(!acquired)return DIERR_NOTACQUIRED;if(!bufferSize)return DIERR_NOTBUFFERED;
  std::vector<BYTE> current(dataSize);HRESULT hr=render(dataSize,current.data());if(FAILED(hr))return hr;
  if(previous.empty())previous.assign(dataSize,0);
  for(auto& f:fields){DWORD len=(f.type&DIDFT_BUTTON)?1:4;if(memcmp(current.data()+f.offset,previous.data()+f.offset,len)){DWORD v=0;memcpy(&v,current.data()+f.offset,len);pushEvent(f.offset,v);}}
  previous=current;DWORD take=std::min(capacity,(DWORD)events.size());
  if(ptr)for(DWORD j=0;j<take;j++)ptr[j]=events[j];
  if(!(flags&DIGDD_PEEK))for(DWORD j=0;j<take;j++)events.pop_front();*count=take;return DI_OK;
 }
 HRESULT STDMETHODCALLTYPE SetDataFormat(LPCDIDATAFORMAT f) override {
  if(real)return real->SetDataFormat(f);
  if(!f||f->dwSize!=sizeof(*f)||f->dwObjSize!=sizeof(DIOBJECTDATAFORMAT)||f->dwDataSize>4096||!f->dwDataSize||!f->rgodf)return DIERR_INVALIDPARAM;
  std::vector<Field> next;int buttonIndex=0,povIndex=0,axisIndex=0;
  for(DWORD i=0;i<f->dwNumObjs;i++) {
   auto& o=f->rgodf[i];DWORD len=(o.dwType&DIDFT_BUTTON)?1:4;
   if(o.dwOfs>f->dwDataSize||len>f->dwDataSize-o.dwOfs)return DIERR_INVALIDPARAM;
   int index=DIDFT_GETINSTANCE(o.dwType);
   if(o.dwType&DIDFT_BUTTON){if((o.dwType&DIDFT_INSTANCEMASK)==DIDFT_ANYINSTANCE)index=buttonIndex;buttonIndex++;}
   if(o.dwType&DIDFT_POV){if((o.dwType&DIDFT_INSTANCEMASK)==DIDFT_ANYINSTANCE)index=povIndex;povIndex++;}
   if(o.dwType&DIDFT_AXIS){index=99;if(o.pguid){if(*o.pguid==GUID_XAxis)index=0;else if(*o.pguid==GUID_YAxis)index=1;}else index=(o.dwType&DIDFT_INSTANCEMASK)==DIDFT_ANYINSTANCE?axisIndex:DIDFT_GETINSTANCE(o.dwType);axisIndex++;if(o.dwFlags&&(o.dwFlags&DIDOI_ASPECTMASK)!=DIDOI_ASPECTPOSITION)index=99;}
   next.push_back({o.dwOfs,o.dwType,index});
  }
  fields=next;dataSize=f->dwDataSize;events.clear();previous.clear();logf("virtual SetDataFormat bytes=%lu objects=%lu",dataSize,f->dwNumObjs);return DI_OK;
 }
 HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE e) override {return real?real->SetEventNotification(e):DIERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND w,DWORD flags) override {
  if(!real)return DI_OK;
  DWORD actual=flags;if(sharedInput)actual=(flags&~(DISCL_EXCLUSIVE|DISCL_NOWINKEY))|DISCL_NONEXCLUSIVE;
  HRESULT hr=real->SetCooperativeLevel(w,actual);logf("SetCooperativeLevel keyboard=%d requested=%lx actual=%lx hr=%08lx",keyboard,flags,actual,hr);return hr;
 }
 HRESULT STDMETHODCALLTYPE GetObjectInfo(LPDIDEVICEOBJECTINSTANCEA p,DWORD obj,DWORD how) override {
  if(real)return real->GetObjectInfo(p,obj,how);if(!p||p->dwSize!=sizeof(*p))return DIERR_INVALIDPARAM;
  for(int k=0;k<(dpadPov?23:22);k++){auto o=object(k);if((how==DIPH_BYID&&o.dwType==obj)||(how==DIPH_BYOFFSET&&o.dwOfs==obj)){*p=o;return DI_OK;}}return DIERR_OBJECTNOTFOUND;
 }
 HRESULT STDMETHODCALLTYPE GetDeviceInfo(LPDIDEVICEINSTANCEA p) override {if(real)return real->GetDeviceInfo(p);if(!p||p->dwSize!=sizeof(*p))return DIERR_INVALIDPARAM;*p=instance();return DI_OK;}
 HRESULT STDMETHODCALLTYPE RunControlPanel(HWND w,DWORD f) override{return real?real->RunControlPanel(w,f):DIERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE h,DWORD v,REFGUID g) override{return real?real->Initialize(h,v,g):DI_OK;}
 HRESULT STDMETHODCALLTYPE CreateEffect(REFGUID g,LPCDIEFFECT e,LPDIRECTINPUTEFFECT* p,LPUNKNOWN u) override {if(real)return real->CreateEffect(g,e,p,u);if(p)*p=nullptr;return DIERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE EnumEffects(LPDIENUMEFFECTSCALLBACKA c,LPVOID x,DWORD f) override{return real?real->EnumEffects(c,x,f):DI_OK;}
 HRESULT STDMETHODCALLTYPE GetEffectInfo(LPDIEFFECTINFOA i,REFGUID g) override{return real?real->GetEffectInfo(i,g):DIERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD p) override {if(real)return real->GetForceFeedbackState(p);if(p)*p=0;return DIERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD c) override{return real?real->SendForceFeedbackCommand(c):DIERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK c,LPVOID x,DWORD f) override{return real?real->EnumCreatedEffectObjects(c,x,f):DI_OK;}
 HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE e) override{return real?real->Escape(e):DIERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE Poll() override{return real?real->Poll():(acquired?DI_OK:DIERR_NOTACQUIRED);}
 HRESULT STDMETHODCALLTYPE SendDeviceData(DWORD s,LPCDIDEVICEOBJECTDATA p,LPDWORD n,DWORD f) override{return real?real->SendDeviceData(s,p,n,f):DIERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE EnumEffectsInFile(LPCSTR s,LPDIENUMEFFECTSINFILECALLBACK c,LPVOID x,DWORD f) override{return real?real->EnumEffectsInFile(s,c,x,f):DIERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE WriteEffectToFile(LPCSTR s,DWORD n,LPDIFILEEFFECT e,DWORD f) override{return real?real->WriteEffectToFile(s,n,e,f):DIERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE BuildActionMap(LPDIACTIONFORMATA a,LPCSTR n,DWORD f) override{return real?real->BuildActionMap(a,n,f):DIERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE SetActionMap(LPDIACTIONFORMATA a,LPCSTR n,DWORD f) override{return real?real->SetActionMap(a,n,f):DIERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE GetImageInfo(LPDIDEVICEIMAGEINFOHEADERA p) override{return real?real->GetImageInfo(p):DIERR_UNSUPPORTED;}
};
class Input final:public IDirectInput8A {
 LONG refs=1;IDirectInput8A* real;
 struct EnumContext {LPDIENUMDEVICESCALLBACKA cb;void* ctx;bool stopped=false;};
 static BOOL CALLBACK enumOther(const DIDEVICEINSTANCEA* i,void* p){auto& c=*(EnumContext*)p;DWORD t=GET_DIDEVICE_TYPE(i->dwDevType);if(t!=DI8DEVTYPE_KEYBOARD&&t!=DI8DEVTYPE_MOUSE)return DIENUM_CONTINUE;BOOL r=c.cb(i,c.ctx);if(!r)c.stopped=true;return r;}
public:
 explicit Input(IDirectInput8A* r):real(r){}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p) override {if(!p)return E_POINTER;*p=nullptr;if(id==IID_IUnknown||id==IID_IDirectInput8A){*p=this;AddRef();return S_OK;}return E_NOINTERFACE;}
 ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs);}
 ULONG STDMETHODCALLTYPE Release() override{ULONG r=InterlockedDecrement(&refs);if(!r){real->Release();delete this;}return r;}
 HRESULT STDMETHODCALLTYPE CreateDevice(REFGUID g,LPDIRECTINPUTDEVICE8A* p,LPUNKNOWN u) override {
  if(!p)return E_POINTER;*p=nullptr;if(u)return CLASS_E_NOAGGREGATION;
  if(g==padGuid){*p=new Device;logf("CreateDevice virtual unified controller");return DI_OK;}
  IDirectInputDevice8A* r=nullptr;HRESULT hr=real->CreateDevice(g,&r,u);
  if(SUCCEEDED(hr)&&r){*p=new Device(r,g==GUID_SysKeyboard);logf("CreateDevice real keyboard=%d",g==GUID_SysKeyboard);}return hr;
 }
 HRESULT STDMETHODCALLTYPE EnumDevices(DWORD t,LPDIENUMDEVICESCALLBACKA cb,LPVOID ctx,DWORD flags) override {
  if(!cb)return DIERR_INVALIDPARAM;logf("EnumDevices type=%lu flags=%lu",t,flags);
  if(t==DI8DEVCLASS_GAMECTRL||t==DI8DEVTYPE_GAMEPAD||t==DI8DEVTYPE_JOYSTICK){if(!(flags&DIEDFL_FORCEFEEDBACK)){auto i=instance();cb(&i,ctx);}return DI_OK;}
  if(t==DI8DEVCLASS_ALL){EnumContext c={cb,ctx};HRESULT hr=real->EnumDevices(t,enumOther,&c,flags);if(SUCCEEDED(hr)&&!c.stopped&&!(flags&DIEDFL_FORCEFEEDBACK)){auto i=instance();cb(&i,ctx);}return hr;}
  return real->EnumDevices(t,cb,ctx,flags);
 }
 HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID g) override{return g==padGuid?DI_OK:real->GetDeviceStatus(g);}
 HRESULT STDMETHODCALLTYPE RunControlPanel(HWND w,DWORD f) override{return real->RunControlPanel(w,f);}
 HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE h,DWORD v) override{return real->Initialize(h,v);}
 HRESULT STDMETHODCALLTYPE FindDevice(REFGUID g,LPCSTR n,LPGUID out) override {if(n&&!strcmp(n,"MUSOUTweaksPC Gamepad")){if(!out)return E_POINTER;*out=padGuid;return DI_OK;}return real->FindDevice(g,n,out);}
 HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(LPCSTR,LPDIACTIONFORMATA,LPDIENUMDEVICESBYSEMANTICSCBA,LPVOID,DWORD) override{return DIERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK,LPDICONFIGUREDEVICESPARAMSA,DWORD,LPVOID) override{return DIERR_UNSUPPORTED;}
};
#ifdef PADFIX_PLATFORM_ONLY
static HRESULT WINAPI platformDISetCooperativeLevel(void* device,HWND window,DWORD flags) {
 auto fn=(HRESULT(WINAPI*)(void*,HWND,DWORD))originalMethod(device,13);if(!fn)return DIERR_INVALIDPARAM;
 DWORD actual=flags;if(sharedInput)actual=(flags&~(DISCL_EXCLUSIVE|DISCL_NOWINKEY))|DISCL_NONEXCLUSIVE;
 HRESULT hr=fn(device,window,actual);logf("DirectInput SetCooperativeLevel requested=%08lx actual=%08lx hr=%08lx",flags,actual,hr);return hr;
}
static HRESULT WINAPI platformDICreateDevice(void* input,REFGUID guid,void** output,LPUNKNOWN outer) {
 auto fn=(HRESULT(WINAPI*)(void*,REFGUID,void**,LPUNKNOWN))originalMethod(input,3);if(!fn)return DIERR_INVALIDPARAM;
 HRESULT hr=fn(input,guid,output,outer);if(sharedInput&&SUCCEEDED(hr)&&output&&*output)hookMethod(*output,13,(void*)platformDISetCooperativeLevel);return hr;
}
#endif
extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE h,DWORD v,REFIID id,void** p,LPUNKNOWN u) {
 static std::once_flag once;static HRESULT (WINAPI* create)(HINSTANCE,DWORD,REFIID,void**,LPUNKNOWN);
 std::call_once(once,[]{initializeAll();HMODULE d=systemDll("dinput8.dll");if(d)create=(decltype(create))GetProcAddress(d,"DirectInput8Create");});
 if(!create)return E_FAIL;if(!p)return E_POINTER;*p=nullptr;
#ifdef PADFIX_PLATFORM_ONLY
 HRESULT hr=create(h,v,id,p,u);
 if(sharedInput&&SUCCEEDED(hr)&&p&&*p&&(id==IID_IDirectInput8A||id==IID_IDirectInput8W)){hookMethod(*p,3,(void*)platformDICreateDevice);logf("DirectInput interface hooked for nonexclusive devices");}
 return hr;
#else
 if(!enabled||id!=IID_IDirectInput8A)return create(h,v,id,p,u);
 IDirectInput8A* r=nullptr;HRESULT hr=create(h,v,id,(void**)&r,u);if(SUCCEEDED(hr)&&r){*p=new Input(r);logf("DirectInput8Create ANSI wrapped");}return hr;
#endif
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID) {if(reason==DLL_PROCESS_ATTACH){selfModule=h;installEarlyHooks();}return TRUE;}
