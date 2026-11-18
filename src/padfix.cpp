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

 // Application-local DirectInput adapter; menu keys are injected only in foreground.
static HMODULE selfModule;
static char iniPath[MAX_PATH], logPath[MAX_PATH];
static int enabled=1, foregroundOnly=1, menuNavigation=1, dpadPov=0, slotConfig=-1;
// 0=off, 1=mirror the D-pad onto the left stick continuously, 2=mirror it only
// for a short pulse on each direction change (menu-friendly, no walking).
static int dpadAsLeftStick=0, dpadPulseMs=50;
// Menus that latch on a state change (rather than reading a held direction)
// need repeated edges: blink the D-pad while it stays held, so the game sees a
// fresh press every period. 0 disables the repeat (the default, so titles that
// already repeat on a held direction keep their original feel).
static int dpadRepeatMs=0, dpadRepeatDelayMs=400;
static WORD dpadRepeatRaw=0;
static bool dpadBlinkOff=false, dpadRepeatLogged=false;
static ULONGLONG dpadHoldSince=0, dpadRepeatAt=0, dpadBlinkUntil=0;
// 1 = the D-pad also synthesizes the keyboard arrow keys on the game's own
// DirectInput keyboard device, which is the only menu path some ports accept
// (SSM6 DX has no patchable menu map). 2 = the arrows go in through SendInput
// instead, for titles that read the keyboard only through their window message
// loop (Sengoku Musou 2). 0 = off.
static int dpadArrowKeys=0;
// Hold-to-repeat cadence of the SendInput arrows: a synthesised key never
// auto-repeats, so a held direction has to be re-pressed by hand.
static int arrowKeyPulseMs=40, arrowKeyGapMs=80;
// Published by the D3D9 layer: 1 while the frame the game just finished has no
// 3D scene in it, i.e. the game is sitting in a menu, loading screen or results
// page. Titles that take both their menu cursor and their character movement
// from the same keyboard path (Sengoku Musou 2) only get arrow injection while
// this is set, so a D-pad press can never walk the character.
static volatile LONG menuSceneActive=0;
static int menuInjectOnly=0;
static volatile LONG lastDpadBits=0;
// Keyboard arrows the adapter is currently reporting as pressed on behalf of the
// D-pad. The real keyboard device is shared, so only ever OR bits in.
static WORD arrowSent=0;
// Buttons that also press the keyboard Enter key. Some Koei UI screens accept
// nothing but a keyboard confirm (Orochi Z's story select, and the popup that
// appears when a second player joins), so the pad has to deliver Enter for them.
// Bitmask over buttonMasks; -1 means auto (B + Start/Menu for Orochi Z).
// How it reaches the game:
//   1 = press Enter on the game's own DirectInput keyboard device (the game has
//       to poll that device for this to work);
//   2 = post WM_KEYDOWN/WM_CHAR/WM_KEYUP to the game window;
//   3 = SendInput, i.e. inject at the same layer as a physical key press so the
//       system itself delivers WM_KEYDOWN/WM_CHAR to the focused window.
// Orochi Z never polls its keyboard device and ignored posted window messages,
// so it uses 3: the OS-generated message is indistinguishable from a real Enter.
// -1 = auto.
static int padEnterMode=-1;
static int padEnterButtons=-1, enterPulseMs=60;
static ULONGLONG enterPulseUntil=0, enterKeyUpAt=0;
static bool enterWasDown=false, enterSent=false;
// Raw pad buttons of the last sample, read by the Enter helpers.
static volatile LONG lastPadButtons=0;
static const BYTE arrowDik[4]={DIK_UP,DIK_DOWN,DIK_LEFT,DIK_RIGHT};
static WORD dpadBridgeLast=0;
static ULONGLONG dpadBridgeUntil=0;
static int leftDeadzone=20, rightThreshold=55, triggerThreshold=40, neutralMs=250;
// How many axes the virtual pad advertises and fills: 2 keeps the original
// left-stick-only device (SSM3/SSM4/SSM6/SSM7/OROCHI Z behaviour), 6 adds the
// triggers and the right stick so titles that steer the camera with axes get it.
static int padAxes=2;
static int triggerAxes=1;
// Only the explicitly assigned first player's source may synthesize menu keys.
static int controllerTakeover=1, secondSlotConfig=-1;
static int menuKeyMode=3;
static bool controllerMenuActive();
static void pollControllerMenu();
static void updateRumble();
static void stopRumble(unsigned player);
static void startControllerWorker();
static DWORD WINAPI nativeGetState(DWORD,XINPUT_STATE*);
static DWORD WINAPI nativeSetState(DWORD,XINPUT_VIBRATION*);
static DWORD (WINAPI *gameGetXState)(DWORD,XINPUT_STATE*);
static DWORD (WINAPI *gameSetXState)(DWORD,XINPUT_VIBRATION*);
// Some ports only accept controllers they recognise by name. DW6 PC keeps a
// whitelist of product names and ignores anything else, so the virtual device
// has to introduce itself as one of them.
static char padName[128]="MUSOUTweaksPC Gamepad";
static bool logging=false;
static std::recursive_mutex mutex;
static DWORD (WINAPI *getXState)(DWORD,XINPUT_STATE*);
static DWORD (WINAPI *setXState)(DWORD,XINPUT_VIBRATION*);
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
static const GUID pad2Guid={0x8f8bf702,0x7253,0x42ae,{0x95,0xf3,0x83,0x2a,0x6d,0x88,0x6e,0x10}};
struct NativeBinding { int device; BYTE axis,padding[3]; int sign,object; };
struct NativeAction { DWORD flags,combine; NativeBinding binding[2]; };
struct NativeMap { NativeAction* actions; DWORD count; BYTE useDevice,padding[3]; DWORD device; };
static_assert(sizeof(NativeBinding)==16&&sizeof(NativeAction)==40&&sizeof(NativeMap)==16);
static NativeMap* nativeMenuMap;
static NativeMap* nativeMenuMapCandidates[3];
static unsigned nativeMenuMapCandidateCount;
static bool nativeMenuMapAmbiguityLogged;
static bool sameBinding(const NativeBinding& a,const NativeBinding& b) {
 return a.device==b.device&&a.axis==b.axis&&a.sign==b.sign&&a.object==b.object;
}
static bool writableMemory(void* ptr,SIZE_T bytes);
// The menu map has keyboard arrows in slot 0 and an unused slot 1. Player
// maps are separate allocations: never synthesize keyboard state to navigate.
static bool isMenuMap(const NativeMap& map) {
 if(!map.actions||map.count!=16||!writableMemory(map.actions,4*sizeof(NativeAction)))return false;
 const int arrows[]={DIK_UP,DIK_DOWN,DIK_LEFT,DIK_RIGHT};
 for(int i=0;i<4;i++) {
  const auto& a=map.actions[i];NativeBinding dpad={};dpad.device=0;dpad.object=12+i;
  if(a.binding[0].device!=8||a.binding[0].object!=arrows[i])return false;
  if(a.binding[1].device!=10&&!sameBinding(a.binding[1],dpad))return false;
 }
 return true;
}
static bool patchMenuMap(NativeMap& map,bool& changed) {
 changed=false;if(!isMenuMap(map))return false;
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
 if((BYTE*)ptr<(BYTE*)m.BaseAddress)return false;
 SIZE_T offset=(SIZE_T)((BYTE*)ptr-(BYTE*)m.BaseAddress);
 return offset<=m.RegionSize&&bytes<=m.RegionSize-offset;
}
static void installMenuDpad();
static void logf(const char* fmt,...) {
 if(!logging||!logPath[0])return;
 std::lock_guard<std::recursive_mutex> lock(mutex);
 FILE* f=nullptr; fopen_s(&f,logPath,"a"); if(!f)return;
 fprintf(f,"[%lu] ",GetTickCount()); va_list a;va_start(a,fmt);vfprintf(f,fmt,a);va_end(a);
 fputc('\n',f);fclose(f);
}
// D-pad state translated into the four keyboard arrows. SSM6 DX menus are
// driven by the keyboard arrows (its gamepad menu path only reads the left
// stick), so the D-pad is delivered through the game's own keyboard device
// instead of the stick: the cursor moves, the character stays where it is.
static WORD arrowDesired() {
 if(!dpadArrowKeys)return 0;
 if((menuInjectOnly||controllerTakeover)&&!menuSceneActive)return 0;
 LONG bits=lastDpadBits;WORD out=0;
 if(bits&XINPUT_GAMEPAD_DPAD_UP)out|=1;
 if(bits&XINPUT_GAMEPAD_DPAD_DOWN)out|=2;
 if(bits&XINPUT_GAMEPAD_DPAD_LEFT)out|=4;
 if(bits&XINPUT_GAMEPAD_DPAD_RIGHT)out|=8;
 return out;
}
// A menu confirm must be a single press, not a held key, so a mapped button
// produces a fixed-length Enter pulse on its rising edge.
static bool enterDesired() {
 if(controllerTakeover)return false;
 if(padEnterMode!=1||!padEnterButtons)return false;
 WORD buttons=(WORD)lastPadButtons;bool down=false;
 for(int i=0;i<10;i++)if((padEnterButtons&(1<<i))&&(buttons&buttonMasks[i])){down=true;break;}
 ULONGLONG now=GetTickCount64();
 if(down&&!enterWasDown){enterWasDown=true;enterPulseUntil=now+(ULONGLONG)enterPulseMs;}
 else if(!down)enterWasDown=false;
 return now<enterPulseUntil;
}
// Polled path: the game re-reads the full 256-byte keyboard state every frame,
// so simply adding the pressed bit is enough and releasing the D-pad releases
// the key. Bits are never cleared, so a physically held arrow keeps working.
static void mergeArrowKeys(BYTE* keys) {
 WORD want=dpadArrowKeys==1?arrowDesired():0;
 if(want){
  static bool logged=false;
  if(!logged){logged=true;logf("D-pad arrow-key injection active on the shared keyboard");}
  if(want&1)keys[DIK_UP]|=0x80;
  if(want&2)keys[DIK_DOWN]|=0x80;
  if(want&4)keys[DIK_LEFT]|=0x80;
  if(want&8)keys[DIK_RIGHT]|=0x80;
 }
 if(enterDesired()){
  static bool enterLogged=false;
  if(!enterLogged){enterLogged=true;logf("pad Enter injection active on the shared keyboard");}
  keys[DIK_RETURN]|=0x80;
 }
}
// Buffered path: the game only sees transitions, so a key-down/key-up pair has
// to be appended to whatever the real keyboard already reported.
static void appendArrowEvents(LPDIDEVICEOBJECTDATA ptr,DWORD* count,DWORD capacity,DWORD flags) {
 WORD want=dpadArrowKeys==1?arrowDesired():0;WORD changed=(WORD)(want^arrowSent);
 bool enterNow=enterDesired();bool enterChanged=enterNow!=enterSent;
 if(!changed&&!enterChanged)return;
 DWORD have=*count,room=capacity>have?capacity-have:0;
 if(!room)return;
 DWORD written=0;WORD applied=0;
 for(int i=0;i<4&&written<room;i++) {
  WORD bit=(WORD)(1<<i);
  if(!(changed&bit))continue;
  DIDEVICEOBJECTDATA e={};e.dwOfs=arrowDik[i];e.dwData=(want&bit)?0x80:0;
  e.dwTimeStamp=GetTickCount();
  if(ptr)ptr[have+written]=e;
  written++;applied|=bit;
 }
 if(enterChanged&&written<room){
  DIDEVICEOBJECTDATA e={};e.dwOfs=DIK_RETURN;e.dwData=enterNow?0x80:0;
  e.dwTimeStamp=GetTickCount();
  if(ptr)ptr[have+written]=e;
  written++;
  if(!(flags&DIGDD_PEEK))enterSent=enterNow;
  static bool enterLogged=false;
  if(!enterLogged){enterLogged=true;logf("pad Enter events queued on the buffered keyboard down=%d",(int)enterNow);}
 }
 // A peek must not consume the transition, otherwise the following real read
 // never reports the press the game is waiting for.
 if(!(flags&DIGDD_PEEK))arrowSent=(WORD)((arrowSent&~applied)|(want&applied));
 *count=have+written;
 static bool logged=false;
 if(!logged&&changed){logged=true;logf("D-pad arrow-key events queued on the buffered keyboard: bits=%x",want);}
}
// SendInput path. Sengoku Musou 2 imports nothing from USER32 but MessageBoxA,
// never calls GetKeyboardState/GetAsyncKeyState and never reads the DirectInput
// keyboard device it creates, so the only layer it actually sees is the window
// message loop: exactly where a physical key press arrives. A synthesised key
// never auto-repeats, so a held direction is chopped into short presses with a
// gap between them, which is what keeps a menu cursor stepping.
static const UINT arrowVk[4]={VK_UP,VK_DOWN,VK_LEFT,VK_RIGHT};
static WORD arrowInputHeld=0,arrowInputDown=0;
static ULONGLONG arrowInputUpAt=0,arrowInputNextAt=0;
static void sendArrowVk(int i,bool down) {
 if(dpadArrowKeys==3){
  // Message path: nothing touches the DirectInput device, so a title that reads
  // its pad/keys through DirectInput cannot see these at all.
  // sample() only runs while the game owns the foreground window, so that is
  // also the window the messages belong on.
  HWND target=GetForegroundWindow();
  if(!target)return;
  const LPARAM scan=(LPARAM)((MapVirtualKeyA(arrowVk[i],MAPVK_VK_TO_VSC)&0xff)<<16);
  // Arrow keys are extended keys on real hardware; bit 24 must match or a title
  // that inspects it treats the press as numeric-keypad input.
  const LPARAM extended=(LPARAM)0x01000000;
  if(down)PostMessageA(target,WM_KEYDOWN,(WPARAM)arrowVk[i],0x00000001|scan|extended);
  else PostMessageA(target,WM_KEYUP,(WPARAM)arrowVk[i],(LPARAM)0xC0000001|scan|extended);
  return;
 }
 INPUT in={};in.type=INPUT_KEYBOARD;
 in.ki.wVk=(WORD)arrowVk[i];
 in.ki.wScan=(WORD)(MapVirtualKeyA(arrowVk[i],MAPVK_VK_TO_VSC)&0xff);
 // Arrow keys are extended keys on real hardware (E0 prefix); without the flag
 // the scancodes would decode as the numeric keypad instead.
 in.ki.dwFlags=KEYEVENTF_SCANCODE|KEYEVENTF_EXTENDEDKEY|(down?0:KEYEVENTF_KEYUP);
 SendInput(1,&in,sizeof(in));
}
static void releaseArrowKeys() {
 for(int i=0;i<4;i++)if(arrowInputDown&(1<<i))sendArrowVk(i,false);
 arrowInputDown=0;arrowInputHeld=0;arrowInputUpAt=0;arrowInputNextAt=0;
}
static void maybeSendArrowKeys() {
 if(dpadArrowKeys<2)return;
 // In a battle scene the arrows would walk the character, so hold them back
 // unless the D3D9 layer reports a menu frame. A direction that was already
 // down is released so the character stops the moment the scene comes back.
 if(menuInjectOnly&&!menuSceneActive){releaseArrowKeys();return;}
 WORD want=arrowDesired();
 ULONGLONG now=GetTickCount64();
 if(want!=arrowInputHeld){
  // Direction changed: release what is no longer wanted and press the new
  // direction immediately, so a single tap is never swallowed by the cadence.
  for(int i=0;i<4;i++)if((arrowInputDown&(1<<i))&&!(want&(1<<i))){sendArrowVk(i,false);arrowInputDown=(WORD)(arrowInputDown&~(1<<i));}
  arrowInputHeld=want;arrowInputUpAt=0;arrowInputNextAt=0;
  for(int i=0;i<4;i++)if((want&(1<<i))&&!(arrowInputDown&(1<<i))){sendArrowVk(i,true);arrowInputDown=(WORD)(arrowInputDown|(1<<i));}
  if(arrowInputDown)arrowInputUpAt=now+(ULONGLONG)arrowKeyPulseMs;
  // One tap is one edge. Do not start the repeat cadence until the normal
  // long-press delay has elapsed, even after the first pulse has been released.
  arrowInputNextAt=now+(ULONGLONG)dpadRepeatDelayMs;
  static bool logged=false;
  if(!logged&&want){logged=true;logf("D-pad arrow-key injection active mode=%d bits=%x pulse=%dms gap=%dms",dpadArrowKeys,want,arrowKeyPulseMs,arrowKeyGapMs);}
  return;
 }
 if(!want)return;
 if(arrowInputDown&&now>=arrowInputUpAt){
  for(int i=0;i<4;i++)if(arrowInputDown&(1<<i))sendArrowVk(i,false);
  arrowInputDown=0;arrowInputNextAt=std::max(arrowInputNextAt,now+(ULONGLONG)arrowKeyGapMs);
 }else if(!arrowInputDown&&now>=arrowInputNextAt){
  for(int i=0;i<4;i++)if(want&(1<<i))sendArrowVk(i,true);
  arrowInputDown=want;arrowInputUpAt=now+(ULONGLONG)arrowKeyPulseMs;
 }
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
 if(!_stricmp(executable,"SM5_WIN.exe"))return "SSM5_Tweaks.ini";
 if(!_stricmp(executable,"Launch.exe"))return "SSM7_Tweaks.ini";
 if(!_stricmp(executable,"SW2.exe"))return "SM2_Tweaks.ini";
 return "OrochiZPadX.ini";
}
static void init() {
 GetModuleFileNameA(selfModule,iniPath,MAX_PATH);
 char* slash=strrchr(iniPath,'\\'); if(slash)slash[1]=0;
 char executablePath[MAX_PATH]={};GetModuleFileNameA(nullptr,executablePath,MAX_PATH);const char* executable=strrchr(executablePath,'\\');executable=executable?executable+1:executablePath;
 strcpy_s(gameExeName,executable);
 // DW6 PC keeps a whitelist of DirectInput product names and ignores every
 // other controller, so its defaults differ from the rest of the family.
 const bool dw6=!_stricmp(executable,"SM5_WIN.exe");(void)dw6;
 strcpy_s(logPath,iniPath);strcat_s(logPath,"MUSOUTweaksPC.log");strcat_s(iniPath,configFileNameForExecutable(executable));
 controllerTakeover=GetPrivateProfileIntA("input","controller_takeover",1,iniPath)!=0;
 secondSlotConfig=std::clamp((int)GetPrivateProfileIntA("input","player2_xinput_slot",-1,iniPath),-1,3);
 slotConfig=std::clamp((int)GetPrivateProfileIntA("padfix","xinput_slot",-1,iniPath),-1,3);
 menuKeyMode=(!_stricmp(executable,"SM5_WIN.exe")||!_stricmp(executable,"SM6TC.exe")||!_stricmp(executable,"SM6EN.exe")||!_stricmp(executable,"SM6JP.exe"))?1:3;
 HMODULE xi=systemDll("xinput1_4.dll");if(!xi)xi=systemDll("xinput1_3.dll");if(!xi)xi=systemDll("xinput9_1_0.dll");
 if(xi){getXState=(decltype(getXState))GetProcAddress(xi,"XInputGetState");setXState=(decltype(setXState))GetProcAddress(xi,"XInputSetState");}
#ifdef PADFIX_PLATFORM_ONLY
 enabled=0;logging=GetPrivateProfileIntA("platform","log",0,iniPath)!=0;
 platformConfigure();
  logf("MUSOUTweaksPC 2.53-rc1 platform-only x86 config=%s",iniPath);
 return;
#else
 auto opt=[](const char* key,int def){return (int)GetPrivateProfileIntA("padfix",key,def,iniPath);};
 enabled=opt("enable",1);logging=opt("log",0)!=0;foregroundOnly=opt("foreground_only",1);
 if(!enabled)controllerTakeover=0;
 menuNavigation=opt("dpad_menu_navigation",1);slotConfig=std::clamp(opt("xinput_slot",-1),-1,3);
 int dpadPovSetting=opt("dpad_pov",-1);dpadPov=dpadPovSetting<0?(dw6?1:0):std::clamp(dpadPovSetting,0,1);
 leftDeadzone=std::clamp(opt("left_deadzone",20),0,90);
 rightThreshold=std::clamp(opt("right_threshold",55),10,95);
 triggerThreshold=std::clamp(opt("trigger_threshold",40),1,254);
 neutralMs=std::clamp(opt("neutral_ms",250),0,5000);
 backend=std::clamp(opt("backend",0),0,2);
 int dpadAsLeftStickSetting=std::clamp(opt("dpad_as_left_stick",0),-1,2);
 dpadPulseMs=std::clamp(opt("dpad_menu_pulse_ms",50),0,1000);
 dpadRepeatMs=std::clamp(opt("dpad_menu_repeat_ms",0),0,1000);
 dpadRepeatDelayMs=std::clamp(opt("dpad_menu_repeat_delay_ms",400),0,3000);
 int dpadArrowKeysSetting=std::clamp(opt("dpad_arrow_keys",-1),-1,3);
 ds4RightX=std::clamp(opt("ds4_right_x",2),0,5);ds4RightY=std::clamp(opt("ds4_right_y",5),0,5);
 int padAxesSetting=std::clamp(opt("pad_axes",0),0,6);
 padAxes=padAxesSetting==0?(dw6?6:2):std::clamp(padAxesSetting,2,6);
 triggerAxes=opt("trigger_axes",dw6?0:1)!=0;
 GetPrivateProfileStringA("padfix","pad_name","",padName,sizeof(padName),iniPath);
 if(!padName[0])strcpy_s(padName,dw6?"Controller (XBOX 360 For Windows)":"MUSOUTweaksPC Gamepad");
 HMODULE x=systemDll("xinput1_4.dll"); if(!x)x=systemDll("xinput9_1_0.dll");
 if(x){getXState=(decltype(getXState))GetProcAddress(x,"XInputGetState");setXState=(decltype(setXState))GetProcAddress(x,"XInputSetState");}
 HMODULE d=systemDll("dinput8.dll");if(d)createSystemInput=(decltype(createSystemInput))GetProcAddress(d,"DirectInput8Create");
 logf("MUSOUTweaksPC 2.53-rc1 x86 enable=%d backend=%d slot=%d menu_navigation=%d XInput=%d pad_axes=%d pad_name=%s dpad_repeat=%d/%d/%d config=%s",enabled,backend,slotConfig,menuNavigation,getXState!=nullptr,padAxes,padName,dpadRepeatMs,dpadRepeatDelayMs,dpadPulseMs,iniPath);
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
 const BYTE sm3Map0[]={0x83,0xec,0x28,0x6a,0x10,0xb9,0x00,0x9d,0xa3,0x00};
 const BYTE sm3Map2[]={0x83,0xec,0x28,0x6a,0x10,0xb9,0x20,0x9d,0xa3,0x00};
 const BYTE sm3Map1[]={0x6a,0x10,0xb9,0x10,0x9d,0xa3,0x00};
 if(!nativeMenuMap&&!_stricmp(name,"ShinSangokumusou3.exe")&&nt->OptionalHeader.SizeOfImage>0x639d20+sizeof(NativeMap)&&
    !memcmp(base+0x1585a0,sm3Map0,sizeof(sm3Map0))&&!memcmp(base+0x1587c0,sm3Map2,sizeof(sm3Map2))&&
    !memcmp(base+0x1589d8,sm3Map1,sizeof(sm3Map1))) {
  nativeMenuMapCandidates[0]=(NativeMap*)(base+0x639d00);
  nativeMenuMapCandidates[1]=(NativeMap*)(base+0x639d10);
  nativeMenuMapCandidates[2]=(NativeMap*)(base+0x639d20);
  nativeMenuMapCandidateCount=3;
  logf("SSM3 mapping-table build signature matched; menu table will be selected by live binding fingerprint");
 }
 if(!nativeMenuMap&&!nativeMenuMapCandidateCount)logf("native menu map unavailable for this EXE; D-pad buttons and POV remain available");
 platformConfigure();
 // Auto mode: SSM3 PC wants a continuous mirror because its menu table cannot
 // be reached. SSM6 DX and DW6 PC use dpad_arrow_keys instead, so their D-pad
 // never reaches the left stick and cannot walk the character. The D-pad stays a
 // normal DirectInput POV/button set that the game can bind on its own.
 int dpadBridgeDefault=!_stricmp(gameExeName,"ShinSangokumusou3.exe")?1:0;
 dpadAsLeftStick=dpadAsLeftStickSetting<0?dpadBridgeDefault:dpadAsLeftStickSetting;
 // SSM6 DX and DW6 PC menus read the keyboard arrows, so the D-pad drives them
 // through the game's own keyboard device instead of the left stick: menus work,
 // the character stays put and the D-pad keeps its own bindings.
 // Sengoku Musou 2 takes its menu cursor and its character movement from the
 // very same keyboard path, so SendInput is used and the D3D9 layer is asked to
 // tell us which frames are menus; outside those the arrows stay off.
 bool arrowDefault=!_stricmp(gameExeName,"SM6TC.exe")||!_stricmp(gameExeName,"SM6EN.exe")||!_stricmp(gameExeName,"SM6JP.exe")
  ||!_stricmp(gameExeName,"SM5_WIN.exe");
 int arrowAuto=arrowDefault?1:(!_stricmp(gameExeName,"SW2.exe")?2:0);
dpadArrowKeys=dpadArrowKeysSetting<0?arrowAuto:dpadArrowKeysSetting;
arrowKeyPulseMs=std::clamp(opt("dpad_arrow_pulse_ms",40),10,500);
arrowKeyGapMs=std::clamp(opt("dpad_arrow_gap_ms",80),10,1000);
menuInjectOnly=std::clamp(opt("menu_inject_only",!_stricmp(gameExeName,"SW2.exe")?1:0),0,1);
logf("D-pad arrow-key synthesis=%d pulse=%dms gap=%dms menu_only=%d",dpadArrowKeys,arrowKeyPulseMs,arrowKeyGapMs,menuInjectOnly);
 // B (bit1) and Start/Menu (bit7) press Enter by default for Orochi Z. Every
 // other title keeps this off: a stray Enter during play is not harmless there.
 int padEnterSetting=opt("pad_enter_buttons",-1);
 bool enterDefault=!_stricmp(gameExeName,"OROCHI_Z_TC.exe");
 padEnterButtons=padEnterSetting<0?(enterDefault?((1<<1)|(1<<7)):0):std::clamp(padEnterSetting,0,1023);
 enterPulseMs=std::clamp(opt("pad_enter_pulse_ms",60),10,500);
 int padEnterModeSetting=std::clamp(opt("pad_enter_mode",-1),-1,3);
 padEnterMode=padEnterModeSetting<0?(enterDefault?3:1):padEnterModeSetting;
 logf("pad Enter buttons=%x pulse=%dms mode=%d",padEnterButtons,enterPulseMs,padEnterMode);
 logf("D-pad to left-stick bridge=%d",dpadAsLeftStick);
#endif
}
static void initializeAll(){static std::once_flag once;std::call_once(once,[]{init();});}
static void installMenuDpad() {
 if(!menuNavigation||dpadArrowKeys)return;
 if(!nativeMenuMap&&nativeMenuMapCandidateCount){
  NativeMap* match=nullptr;unsigned matches=0;
  for(unsigned i=0;i<nativeMenuMapCandidateCount;i++){
   NativeMap* candidate=nativeMenuMapCandidates[i];
   if(writableMemory(candidate,sizeof(*candidate))&&isMenuMap(*candidate)){match=candidate;matches++;}
  }
  if(matches!=1){
   if(matches>1&&!nativeMenuMapAmbiguityLogged){nativeMenuMapAmbiguityLogged=true;logf("SSM3 menu map selection rejected: %u tables matched; no player map changed",matches);}
   return;
  }
  nativeMenuMap=match;logf("SSM3 native menu map selected by binding fingerprint: %p",nativeMenuMap);
 }
 if(!nativeMenuMap)return;
 if(!writableMemory(nativeMenuMap,sizeof(*nativeMenuMap))||nativeMenuMap->count!=16)return;
 if(!writableMemory(nativeMenuMap->actions,4*sizeof(NativeAction)))return;
 bool changed=false;
 if(patchMenuMap(*nativeMenuMap,changed)&&changed)logf("native menu D-pad installed: actions=0..3 buttons=13..16; player maps untouched");
}
struct PadState {
 LONG x=0,y=0,rx=0,ry=0,z=-255,rz=-255;
 BYTE buttons[20]={};
 WORD dpad=0;
};
static int selectedSlot=-1,secondSlot=-1;
static bool armed=false, wasConnected=false, wasForeground=false, neutralPending=false;
static int loggedForeground=-1;
static ULONGLONG neutralSince;
static ULONGLONG lastConnection;
static WORD loggedDpad=0xffff;
static bool foreground() {
 if(!foregroundOnly)return true;DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
 return pid==GetCurrentProcessId();
}
// Hold-to-repeat. A menu that latches one cursor step per state change will not
// move any further while the direction stays held, so the forwarded D-pad has to
// be released and pressed again. After dpad_menu_repeat_delay_ms the direction is
// blinked off for dpad_menu_pulse_ms every dpad_menu_repeat_ms, which gives the
// POV hat, the four D-pad buttons, the arrow-key injection and (only when it is
// enabled) the left-stick bridge fresh edges each period.
// dpad_menu_repeat_ms=0 disables the repeat entirely.
static WORD dpadHoldRepeat(WORD raw) {
 if(dpadRepeatMs<=0)return raw;
 ULONGLONG now=GetTickCount64();
 if(raw!=dpadRepeatRaw){
  dpadRepeatRaw=raw;dpadHoldSince=now;dpadBlinkOff=false;dpadBlinkUntil=0;
  dpadRepeatAt=now+(ULONGLONG)dpadRepeatDelayMs;dpadRepeatLogged=false;
  return raw;
 }
 if(!raw)return 0;
 if(dpadBlinkOff){
  if(now<dpadBlinkUntil)return 0;
  // Release finished: hold the direction again for whatever is left of the
  // period, so the cadence stays one step per dpad_menu_repeat_ms.
  int hold=dpadRepeatMs-(dpadPulseMs>0?dpadPulseMs:1);if(hold<1)hold=1;
  dpadBlinkOff=false;dpadRepeatAt=now+(ULONGLONG)hold;
  return raw;
 }
 if(now>=dpadRepeatAt){
  dpadBlinkOff=true;
  dpadBlinkUntil=now+(ULONGLONG)(dpadPulseMs>0?dpadPulseMs:1);
  if(!dpadRepeatLogged){dpadRepeatLogged=true;logf("D-pad repeat active bits=%x period=%dms release=%dms",raw,dpadRepeatMs,dpadPulseMs);}
  return 0;
 }
 return raw;
}
// Hardware-level injection. SendInput feeds the input stack, so Windows builds
// the WM_KEYDOWN/WM_CHAR/WM_KEYUP pair itself and posts it to the focused window
// with a real scan code and a real key-state transition. That is the only path
// that also satisfies a window procedure which re-reads the OS keyboard state
// instead of trusting the message parameters.
static void sendEnterKey(bool down) {
 INPUT in={};in.type=INPUT_KEYBOARD;in.ki.wVk=VK_RETURN;
 in.ki.wScan=(WORD)(MapVirtualKeyA(VK_RETURN,MAPVK_VK_TO_VSC)&0xff);
 in.ki.dwFlags=KEYEVENTF_SCANCODE|(down?0:KEYEVENTF_KEYUP);
 UINT sent=SendInput(1,&in,sizeof(in));
 static int logged=-1;
 if(logged!=(int)down){logged=(int)down;logf("pad Enter SendInput %s sent=%u err=%lu",down?"down":"up",sent,GetLastError());}
}
// Window-message path. Koei ports of this era handle WM_KEYDOWN on the game
// window, which is why synthesised input from outside has to go through
// PostMessage as well. One Enter press per physical button press: the three
// messages are queued back to back, so the game sees a complete down/char/up
// triple.
static void maybePostEnter(WORD buttons) {
 if(padEnterMode!=2||!padEnterButtons)return;
 bool down=false;
 for(int i=0;i<10;i++)if((padEnterButtons&(1<<i))&&(buttons&buttonMasks[i])){down=true;break;}
 if(!down){enterWasDown=false;return;}
 if(enterWasDown)return;
 enterWasDown=true;
 HWND target=gameWindow?gameWindow:GetForegroundWindow();
 if(!target){logf("pad Enter: no window to post to");return;}
 const LPARAM scan=(LPARAM)((MapVirtualKeyA(VK_RETURN,MAPVK_VK_TO_VSC)&0xff)<<16);
 BOOL a=PostMessageA(target,WM_KEYDOWN,VK_RETURN,0x00000001|scan);
 BOOL b=PostMessageA(target,WM_CHAR,VK_RETURN,0x00000001|scan);
 BOOL c=PostMessageA(target,WM_KEYUP,VK_RETURN,(LPARAM)0xC0000001|scan);
 static bool logged=false;
 if(!logged){logged=true;logf("pad Enter posted to hwnd=%p as WM_KEYDOWN/WM_CHAR/WM_KEYUP ok=%d/%d/%d err=%lu",target,a,b,c,GetLastError());}
}
// SendInput path. A menu confirm must be a pulse, not a held key, so the press
// is released enterPulseMs later from the following sample instead of blocking
// the render thread with a sleep.
static void maybeSendEnter(WORD buttons) {
 if(padEnterMode!=3||!padEnterButtons)return;
 bool down=false;
 for(int i=0;i<10;i++)if((padEnterButtons&(1<<i))&&(buttons&buttonMasks[i])){down=true;break;}
 ULONGLONG now=GetTickCount64();
 if(down&&!enterWasDown){
  enterWasDown=true;
  if(!enterKeyUpAt){sendEnterKey(true);enterKeyUpAt=now+(ULONGLONG)enterPulseMs;}
 }
 if(!down)enterWasDown=false;
 if(enterKeyUpAt&&now>=enterKeyUpAt){enterKeyUpAt=0;sendEnterKey(false);}
}
// Never leave the synthetic key stuck down when the pad drops out or the window
// stops being the foreground one mid-pulse.
static void releaseEnterKey() {
 if(enterKeyUpAt){enterKeyUpAt=0;sendEnterKey(false);}
}
static PadState sample() {
 std::lock_guard<std::recursive_mutex> lock(mutex);
 lastDpadBits=0;lastPadButtons=0;
 PadState out; XINPUT_STATE s={}; bool connected=false;
 if(getXState&&backend!=2&&selectedBackend!=2) {
  if(slotConfig>=0||controllerTakeover){selectedSlot=slotConfig>=0?slotConfig:0;connected=getXState(selectedSlot,&s)==ERROR_SUCCESS;}
  else if(selectedSlot>=0)connected=getXState(selectedSlot,&s)==ERROR_SUCCESS;
  else for(int i=0;i<4;i++)if(getXState(i,&s)==ERROR_SUCCESS){selectedSlot=i;connected=true;break;}
 }
 if(connected)selectedBackend=1;
 if(!connected&&backend!=1&&selectedBackend!=1&&readDs4(s)){connected=true;selectedBackend=2;}
 if(connected)lastConnection=GetTickCount64();
 // Preserve the selected source through short dropouts; rescan after unplug.
 if(!connected&&selectedBackend&&GetTickCount64()-lastConnection>=1500){
  if(backend==0&&!controllerTakeover)selectedBackend=0;
  // With menu ownership enabled, never promote 2P after 1P disconnects.
  if(slotConfig<0&&!controllerTakeover)selectedSlot=-1;
 }
 bool fg=foreground();
 if(loggedForeground!=(int)fg){loggedForeground=(int)fg;logf("controller game_foreground=%d",fg);}
 if(connected!=wasConnected){logf("controller %s backend=%s slot=%d",connected?"connected":"disconnected",selectedBackend==2?"DS4":"XInput",selectedSlot);wasConnected=connected;}
 if(!connected||!fg||!wasForeground){armed=false;neutralPending=false;}
 wasForeground=fg;
 if(!connected||!fg){releaseEnterKey();releaseArrowKeys();stopRumble(0);return out;}
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
 // Published for the Enter helpers, and the window-message path runs right here
 // because this is the call the game makes every frame.
 lastPadButtons=(LONG)g.wButtons;
 if(!controllerTakeover){maybePostEnter(g.wButtons);maybeSendEnter(g.wButtons);}
 out.x=axis(g.sThumbLX);out.y=-axis(g.sThumbLY);
 // Right stick keeps the deadzone so a resting stick never pans the camera.
 out.rx=axis(g.sThumbRX);out.ry=-axis(g.sThumbRY);
 // Triggers become full-range axes: fully released maps to the axis minimum so
 // a title that treats the axis as "how far pressed" reads 0 at rest.
 out.z=(LONG)g.bLeftTrigger*510/255-255;out.rz=(LONG)g.bRightTrigger*510/255-255;
 // DW6's native map zoom reads the trigger axis independently of its bindable
 // buttons. Keep the LT/RT buttons, but park the duplicate axes at rest.
 if(!triggerAxes)out.z=out.rz=-255;
 for(int i=0;i<10;i++)out.buttons[i]=(g.wButtons&buttonMasks[i])?0x80:0;
 out.buttons[10]=g.bLeftTrigger>=triggerThreshold?0x80:0;
 out.buttons[11]=g.bRightTrigger>=triggerThreshold?0x80:0;
 WORD rawDpad=g.wButtons&15;
 out.dpad=rawDpad;
 static WORD loggedForwardedDpad=0xffff;
 if(out.dpad!=loggedForwardedDpad){loggedForwardedDpad=out.dpad;logf("virtual DirectInput forwarded D-pad bits=%x left=%ld,%ld",out.dpad,out.x,out.y);}
 // Held directions become repeated presses here, after the raw state has been
 // logged, so the POV hat, the buttons, the arrow keys and the left-stick
 // bridge all see the same blinking D-pad.
 out.dpad=dpadHoldRepeat(rawDpad);
 out.buttons[12]=(out.dpad&XINPUT_GAMEPAD_DPAD_UP)?0x80:0;
 out.buttons[13]=(out.dpad&XINPUT_GAMEPAD_DPAD_DOWN)?0x80:0;
 out.buttons[14]=(out.dpad&XINPUT_GAMEPAD_DPAD_LEFT)?0x80:0;
 out.buttons[15]=(out.dpad&XINPUT_GAMEPAD_DPAD_RIGHT)?0x80:0;
 out.buttons[16]=g.sThumbRY>rt?0x80:0;out.buttons[17]=g.sThumbRY<-rt?0x80:0;
 out.buttons[18]=g.sThumbRX<-rt?0x80:0;out.buttons[19]=g.sThumbRX>rt?0x80:0;
 // The SM3 PC build, SSM6 DX and DW6 PC do not consume the POV/button D-pad
 // through their native menu maps, so the D-pad is mirrored onto the left stick.
 // Mode 1 mirrors continuously (menus repeat, but the character also walks).
 // Mode 2 mirrors only for a short pulse per direction change: long enough for
 // a menu to latch one cursor step, short enough that gameplay barely moves.
 // Both modes follow the repeated (blinked) state, because that edge is what
 // makes a held direction keep stepping through a menu.
 if(dpadAsLeftStick){
  bool apply=dpadAsLeftStick==1;
  if(!apply){
   if(out.dpad!=dpadBridgeLast){
    dpadBridgeLast=out.dpad;
    dpadBridgeUntil=out.dpad?GetTickCount64()+(ULONGLONG)dpadPulseMs:0;
   }
   apply=out.dpad&&GetTickCount64()<dpadBridgeUntil;
  }
  if(apply){
   if(out.dpad&XINPUT_GAMEPAD_DPAD_LEFT)out.x=-255;
   else if(out.dpad&XINPUT_GAMEPAD_DPAD_RIGHT)out.x=255;
   // SM3 reads the virtual DirectInput Y axis opposite to this adapter's prior
   // assumption: positive moves the menu down, so invert the D-pad bridge.
   if(out.dpad&XINPUT_GAMEPAD_DPAD_UP)out.y=-255;
   else if(out.dpad&XINPUT_GAMEPAD_DPAD_DOWN)out.y=255;
  }
 }
 lastDpadBits=dpadArrowKeys>=2?rawDpad:out.dpad;
 // Last, so the SendInput path sees this frame's direction rather than the
 // previous one.
 maybeSendArrowKeys();
 if(controllerMenuActive())out.buttons[0]=out.buttons[1]=0;
 // Keyboard navigation owns the menu direction: never deliver the same edge
 // through both the pad POV/buttons and the keyboard as well.
 if(dpadArrowKeys&&menuSceneActive){
  out.dpad=0;for(int i=12;i<16;i++)out.buttons[i]=0;
 }
 return out;
}
static PadState sampleSecond() {
 std::lock_guard<std::recursive_mutex> lock(mutex);
 PadState out;XINPUT_STATE s={};
 if(secondSlotConfig>=0)secondSlot=secondSlotConfig;
 if(secondSlot<0&&getXState)for(int i=0;i<4;i++)if(i!=selectedSlot&&getXState(i,&s)==ERROR_SUCCESS){secondSlot=i;break;}
 static bool ready=false;
 if(secondSlot<0||secondSlot==selectedSlot||!getXState||getXState(secondSlot,&s)!=ERROR_SUCCESS||!foreground()){
  ready=false;stopRumble(1);return out;
 }
 auto& g=s.Gamepad;int dz=leftDeadzone*32767/100,rt=rightThreshold*32767/100;
 if(!ready){ready=!g.wButtons&&g.bLeftTrigger<triggerThreshold&&g.bRightTrigger<triggerThreshold&&abs((int)g.sThumbLX)<=dz&&abs((int)g.sThumbLY)<=dz&&abs((int)g.sThumbRX)<rt&&abs((int)g.sThumbRY)<rt;return out;}
 if(controllerTakeover&&menuSceneActive)return out;
 auto axis=[dz](int v)->LONG{return abs(v)<=dz?0:std::clamp(v,-32767,32767)*255/32767;};
 out.x=axis(g.sThumbLX);out.y=-axis(g.sThumbLY);out.rx=axis(g.sThumbRX);out.ry=-axis(g.sThumbRY);
 if(triggerAxes){out.z=g.bLeftTrigger*2-255;out.rz=g.bRightTrigger*2-255;}
 for(int i=0;i<10;i++)out.buttons[i]=(g.wButtons&buttonMasks[i])?0x80:0;
 out.buttons[10]=g.bLeftTrigger>=triggerThreshold?0x80:0;out.buttons[11]=g.bRightTrigger>=triggerThreshold?0x80:0;
 out.dpad=g.wButtons&15;for(int i=0;i<4;i++)out.buttons[12+i]=(out.dpad&(1<<i))?0x80:0;
 out.buttons[16]=g.sThumbRY>rt?0x80:0;out.buttons[17]=g.sThumbRY<-rt?0x80:0;
 out.buttons[18]=g.sThumbRX<-rt?0x80:0;out.buttons[19]=g.sThumbRX>rt?0x80:0;
 return out;
}
#include "controller.inc"
static DIDEVICEINSTANCEA instance(unsigned player=0) {
 DIDEVICEINSTANCEA i={};i.dwSize=sizeof(i);i.guidInstance=i.guidProduct=padGuid;
 if(player)i.guidInstance=i.guidProduct=pad2Guid;
 i.dwDevType=DI8DEVTYPE_GAMEPAD|(DI8DEVTYPEGAMEPAD_STANDARD<<8);
 strcpy_s(i.tszInstanceName,padName);strcpy_s(i.tszProductName,padName);return i;
}
// Axis slots follow the c_dfDIJoystick2 order: X, Y, Z, Rx, Ry, Rz.
static const GUID* const axisGuid[6]={&GUID_XAxis,&GUID_YAxis,&GUID_ZAxis,&GUID_RxAxis,&GUID_RyAxis,&GUID_RzAxis};
static const char* const axisLabel[6]={"Left stick X","Left stick Y","Left trigger","Right stick X","Right stick Y","Right trigger"};
static int axisSlot(const GUID& g) {
 for(int i=0;i<6;i++)if(g==*axisGuid[i])return i;
 return -1;
}
struct Field { DWORD offset,type;int index; };
class Device final:public IDirectInputDevice8A {
 LONG refs=1; IDirectInputDevice8A* real=nullptr; bool keyboard=false,acquired=false;
 bool keyboardPollLogged=false,keyboardBufferedLogged=false,keyboardAcquireLogged=false;
 unsigned player=0;ULONGLONG menuCursor=0;
 std::vector<Field> fields; DWORD dataSize=0,bufferSize=0,sequence=0;
 LONG minimum[6]={0,0,0,0,0,0},maximum[6]={65535,65535,65535,65535,65535,65535};
 std::vector<BYTE> previous; std::deque<DIDEVICEOBJECTDATA> events;
 DWORD resolveAxis(const DIPROPHEADER* h) {
  // 6 = device-wide, 99 = not an axis this device reports.
  if(h->dwHow==DIPH_DEVICE)return 6;
  if(h->dwHow==DIPH_BYID){DWORD i=DIDFT_GETINSTANCE(h->dwObj);return i<6?i:99;}
  if(h->dwHow==DIPH_BYOFFSET)for(auto& f:fields)if(f.offset==h->dwObj&&(f.type&DIDFT_AXIS))return f.index<6?(DWORD)f.index:99;
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
  auto s=player?sampleSecond():sample();memset(ptr,0,n);
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
    if(i<padAxes){
     LONG a=0;
     switch(i){case 0:a=s.x;break;case 1:a=s.y;break;case 2:a=s.z;break;
      case 3:a=s.rx;break;case 4:a=s.ry;break;default:a=s.rz;}
     v=(LONG)((long long)minimum[i]+((long long)a+255)*((long long)maximum[i]-minimum[i])/510);
    }
    memcpy(dst,&v,4);
   }
  }
  return DI_OK;
 }
 DIDEVICEOBJECTINSTANCEA object(int k) {
  DIDEVICEOBJECTINSTANCEA o={};o.dwSize=sizeof(o);
  if(k<padAxes){o.guidType=*axisGuid[k];o.dwOfs=k*4;o.dwType=DIDFT_ABSAXIS|DIDFT_MAKEINSTANCE(k);if(k<2){o.dwType|=DIDFT_FFACTUATOR;o.dwFlags=DIDOI_FFACTUATOR;}strcpy_s(o.tszName,axisLabel[k]);}
  else if(k==padAxes&&dpadPov){o.guidType=GUID_POV;o.dwOfs=DIJOFS_POV(0);o.dwType=DIDFT_POV|DIDFT_MAKEINSTANCE(0);strcpy_s(o.tszName,"D-pad POV");}
  else{int b=k-(padAxes+(dpadPov?1:0));o.guidType=GUID_Button;o.dwOfs=DIJOFS_BUTTON(b);o.dwType=DIDFT_PSHBUTTON|DIDFT_MAKEINSTANCE(b);sprintf_s(o.tszName,"Button %d",b+1);}
  return o;
 }
public:
 explicit Device(IDirectInputDevice8A* r=nullptr,bool kb=false,unsigned p=0):real(r),keyboard(kb),player(p),menuCursor(menuSerial){}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p) override {if(!p)return E_POINTER;*p=nullptr;if(id==IID_IUnknown||id==IID_IDirectInputDevice8A){*p=this;AddRef();return S_OK;}return E_NOINTERFACE;}
 ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs);}
 ULONG STDMETHODCALLTYPE Release() override{ULONG r=InterlockedDecrement(&refs);if(!r){if(real)real->Release();else stopRumble(player);delete this;}return r;}
 HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS c) override {
  if(real)return real->GetCapabilities(c);if(!c||c->dwSize!=sizeof(*c))return DIERR_INVALIDPARAM;
  DWORD size=c->dwSize;memset(c,0,sizeof(*c));c->dwSize=size;c->dwFlags=DIDC_ATTACHED|DIDC_EMULATED|DIDC_FORCEFEEDBACK;
  c->dwFFSamplePeriod=1000;c->dwFFMinTimeResolution=1000;
  c->dwDevType=instance(player).dwDevType;c->dwAxes=padAxes;c->dwButtons=20;c->dwPOVs=dpadPov?1:0;return DI_OK;
 }
 HRESULT STDMETHODCALLTYPE EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKA cb,LPVOID ctx,DWORD flags) override {
  if(real)return real->EnumObjects(cb,ctx,flags);if(!cb)return DIERR_INVALIDPARAM;
  for(int i=0;i<padAxes+(dpadPov?1:0)+20;i++){auto o=object(i);if(flags==DIDFT_ALL||DIDFT_GETTYPE(flags&o.dwType))if(cb(&o,ctx)==DIENUM_STOP)break;}return DI_OK;
 }
 HRESULT STDMETHODCALLTYPE GetProperty(REFGUID prop,LPDIPROPHEADER h) override {
  if(real)return real->GetProperty(prop,h);if(!h)return DIERR_INVALIDPARAM;
  if(&prop==&DIPROP_RANGE){if(h->dwSize!=sizeof(DIPROPRANGE))return DIERR_INVALIDPARAM;DWORD a=resolveAxis(h);if(a==99)return DIERR_OBJECTNOTFOUND;auto p=(DIPROPRANGE*)h;p->lMin=minimum[a==6?0:a];p->lMax=maximum[a==6?0:a];return DI_OK;}
  if(&prop==&DIPROP_BUFFERSIZE||&prop==&DIPROP_DEADZONE||&prop==&DIPROP_SATURATION||&prop==&DIPROP_AXISMODE){if(h->dwSize!=sizeof(DIPROPDWORD))return DIERR_INVALIDPARAM;((DIPROPDWORD*)h)->dwData=&prop==&DIPROP_BUFFERSIZE?bufferSize:(&prop==&DIPROP_AXISMODE?DIPROPAXISMODE_ABS:0);return DI_OK;}
  if(&prop==&DIPROP_FFGAIN||&prop==&DIPROP_AUTOCENTER){if(h->dwSize!=sizeof(DIPROPDWORD))return DIERR_INVALIDPARAM;((DIPROPDWORD*)h)->dwData=&prop==&DIPROP_FFGAIN?rumbleGain[player]:0;return DI_OK;}
  return DIERR_UNSUPPORTED;
 }
 HRESULT STDMETHODCALLTYPE SetProperty(REFGUID prop,LPCDIPROPHEADER h) override {
  if(real){HRESULT hr=real->SetProperty(prop,h);if(SUCCEEDED(hr)&&keyboard&&&prop==&DIPROP_BUFFERSIZE&&h&&h->dwSize==sizeof(DIPROPDWORD))bufferSize=((DIPROPDWORD*)h)->dwData;return hr;}
  if(!h)return DIERR_INVALIDPARAM;
  if(&prop==&DIPROP_RANGE){if(h->dwSize!=sizeof(DIPROPRANGE))return DIERR_INVALIDPARAM;auto p=(const DIPROPRANGE*)h;if(p->lMin>=p->lMax)return DIERR_INVALIDPARAM;DWORD a=resolveAxis(h);if(a==99)return DIERR_OBJECTNOTFOUND;for(int k=0;k<6;k++)if(a==6||a==(DWORD)k){minimum[k]=p->lMin;maximum[k]=p->lMax;}return DI_OK;}
  if(&prop==&DIPROP_BUFFERSIZE){if(h->dwSize!=sizeof(DIPROPDWORD))return DIERR_INVALIDPARAM;bufferSize=std::min(((const DIPROPDWORD*)h)->dwData,1024UL);events.clear();previous.clear();return DI_OK;}
  if(&prop==&DIPROP_AXISMODE){if(h->dwSize!=sizeof(DIPROPDWORD))return DIERR_INVALIDPARAM;return ((const DIPROPDWORD*)h)->dwData==DIPROPAXISMODE_ABS?DI_OK:DIERR_UNSUPPORTED;}
  if(&prop==&DIPROP_DEADZONE||&prop==&DIPROP_SATURATION)return DI_OK;
  if(&prop==&DIPROP_FFGAIN||&prop==&DIPROP_AUTOCENTER){if(h->dwSize!=sizeof(DIPROPDWORD))return DIERR_INVALIDPARAM;if(&prop==&DIPROP_FFGAIN){rumbleGain[player]=std::min(((const DIPROPDWORD*)h)->dwData,10000UL);updateRumble();}return DI_OK;}
  return DIERR_UNSUPPORTED;
 }
 HRESULT STDMETHODCALLTYPE Acquire() override {
  if(!real){acquired=true;return DI_OK;}
  HRESULT hr=real->Acquire();
  if(keyboard&&!keyboardAcquireLogged){keyboardAcquireLogged=true;logf("keyboard Acquire hr=%08lx",hr);}
  return hr;
 }
 HRESULT STDMETHODCALLTYPE Unacquire() override {if(real)return real->Unacquire();stopRumble(player);acquired=false;events.clear();previous.clear();return DI_OK;}
 HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD n,LPVOID p) override {
  if(!real)return render(n,p);
  HRESULT hr=real->GetDeviceState(n,p);
  if(keyboard&&!keyboardPollLogged){keyboardPollLogged=true;logf("keyboard GetDeviceState size=%lu hr=%08lx",n,hr);}
  if(SUCCEEDED(hr)&&keyboard&&p&&n==256){std::lock_guard<std::recursive_mutex> lock(mutex);sample();pollControllerMenu();mergeArrowKeys((BYTE*)p);mergeMenuKeys((BYTE*)p);}
  return hr;
 }
 HRESULT STDMETHODCALLTYPE GetDeviceData(DWORD stride,LPDIDEVICEOBJECTDATA ptr,LPDWORD count,DWORD flags) override {
  if(!count||stride!=sizeof(DIDEVICEOBJECTDATA)||flags&~DIGDD_PEEK)return DIERR_INVALIDPARAM;
  DWORD capacity=*count;*count=0;
  if(real){
   *count=capacity;HRESULT hr=real->GetDeviceData(stride,ptr,count,flags);
   if(keyboard&&!keyboardBufferedLogged){keyboardBufferedLogged=true;logf("keyboard GetDeviceData capacity=%lu hr=%08lx",capacity,hr);}
   if(SUCCEEDED(hr)&&keyboard){std::lock_guard<std::recursive_mutex> lock(mutex);sample();pollControllerMenu();filterMenuEvents(ptr,count);appendArrowEvents(ptr,count,capacity,flags);appendMenuEvents(ptr,count,capacity,flags,menuCursor);}
   return hr;
  }
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
   if(o.dwType&DIDFT_AXIS){index=99;if(o.pguid){int slot=axisSlot(*o.pguid);if(slot>=0)index=slot;}else index=(o.dwType&DIDFT_INSTANCEMASK)==DIDFT_ANYINSTANCE?axisIndex:DIDFT_GETINSTANCE(o.dwType);axisIndex++;if(o.dwFlags&&(o.dwFlags&DIDOI_ASPECTMASK)!=DIDOI_ASPECTPOSITION)index=99;}
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
  for(int k=0;k<padAxes+(dpadPov?1:0)+20;k++){auto o=object(k);if((how==DIPH_BYID&&(o.dwType&(0x00ffffff))==(obj&(0x00ffffff)))||(how==DIPH_BYOFFSET&&o.dwOfs==obj)){*p=o;return DI_OK;}}return DIERR_OBJECTNOTFOUND;
 }
 HRESULT STDMETHODCALLTYPE GetDeviceInfo(LPDIDEVICEINSTANCEA p) override {if(real)return real->GetDeviceInfo(p);if(!p||p->dwSize!=sizeof(*p))return DIERR_INVALIDPARAM;*p=instance(player);return DI_OK;}
 HRESULT STDMETHODCALLTYPE RunControlPanel(HWND w,DWORD f) override{return real?real->RunControlPanel(w,f):DIERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE h,DWORD v,REFGUID g) override{return real?real->Initialize(h,v,g):DI_OK;}
 HRESULT STDMETHODCALLTYPE CreateEffect(REFGUID g,LPCDIEFFECT e,LPDIRECTINPUTEFFECT* p,LPUNKNOWN u) override {
  if(real)return real->CreateEffect(g,e,p,u);if(!p)return E_POINTER;*p=nullptr;if(u)return CLASS_E_NOAGGREGATION;if(!supportsEffect(g))return DIERR_UNSUPPORTED;
  std::lock_guard<std::recursive_mutex> lock(mutex);auto effect=new PadEffect(g,player);if(e){HRESULT hr=effect->SetParameters(e,DIEP_ALLPARAMS);if(FAILED(hr)){effect->Release();return hr;}}*p=effect;return DI_OK;
 }
 HRESULT STDMETHODCALLTYPE EnumEffects(LPDIENUMEFFECTSCALLBACKA c,LPVOID x,DWORD f) override {
  if(real)return real->EnumEffects(c,x,f);if(!c)return DIERR_INVALIDPARAM;
  const GUID* kinds[]={&GUID_ConstantForce,&GUID_RampForce,&GUID_Sine,&GUID_Square,&GUID_Triangle,&GUID_SawtoothUp,&GUID_SawtoothDown};
  for(auto k:kinds){auto i=effectInfo(*k);if((!f||DIEFT_GETTYPE(f)==DIEFT_GETTYPE(i.dwEffType))&&c(&i,x)==DIENUM_STOP)break;}return DI_OK;
 }
 HRESULT STDMETHODCALLTYPE GetEffectInfo(LPDIEFFECTINFOA i,REFGUID g) override{if(real)return real->GetEffectInfo(i,g);if(!i||i->dwSize!=sizeof(*i))return DIERR_INVALIDPARAM;if(!supportsEffect(g))return DIERR_OBJECTNOTFOUND;*i=effectInfo(g);return DI_OK;}
 HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD p) override {if(real)return real->GetForceFeedbackState(p);if(!p)return E_POINTER;*p=DIGFFS_POWERON|(rumbleEnabled[player]?DIGFFS_ACTUATORSON:DIGFFS_ACTUATORSOFF)|(rumblePaused[player]?DIGFFS_PAUSED:0);return DI_OK;}
 HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD c) override{
  if(real)return real->SendForceFeedbackCommand(c);std::lock_guard<std::recursive_mutex> lock(mutex);
  switch(c){case DISFFC_STOPALL:stopRumble(player);break;case DISFFC_RESET:stopRumble(player);for(auto e:padEffects)if(e->player==player)e->downloaded=false;rumbleGain[player]=10000;rumblePaused[player]=false;rumbleEnabled[player]=true;break;
   case DISFFC_PAUSE:if(!rumblePaused[player])rumblePauseAt[player]=GetTickCount64();rumblePaused[player]=true;break;
   case DISFFC_CONTINUE:if(rumblePaused[player])for(auto e:padEffects)if(e->player==player&&e->running)e->started+=GetTickCount64()-rumblePauseAt[player];rumblePaused[player]=false;break;
   case DISFFC_SETACTUATORSON:rumbleEnabled[player]=true;break;case DISFFC_SETACTUATORSOFF:rumbleEnabled[player]=false;break;default:return DIERR_INVALIDPARAM;}updateRumble();return DI_OK;
 }
 HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK c,LPVOID x,DWORD f) override{
  if(real)return real->EnumCreatedEffectObjects(c,x,f);if(!c)return DIERR_INVALIDPARAM;std::lock_guard<std::recursive_mutex> lock(mutex);auto snapshot=padEffects;for(auto e:snapshot)e->AddRef();bool more=true;for(auto e:snapshot){if(more&&e->player==player)more=c(e,x)!=DIENUM_STOP;e->Release();}return DI_OK;
 }
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
 bool nativePads=false;
 struct EnumContext {LPDIENUMDEVICESCALLBACKA cb;void* ctx;bool stopped=false;};
 static BOOL CALLBACK enumOther(const DIDEVICEINSTANCEA* i,void* p){auto& c=*(EnumContext*)p;DWORD t=GET_DIDEVICE_TYPE(i->dwDevType);if(t!=DI8DEVTYPE_KEYBOARD&&t!=DI8DEVTYPE_MOUSE)return DIENUM_CONTINUE;BOOL r=c.cb(i,c.ctx);if(!r)c.stopped=true;return r;}
public:
 explicit Input(IDirectInput8A* r,bool native=false):real(r),nativePads(native){}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p) override {if(!p)return E_POINTER;*p=nullptr;if(id==IID_IUnknown||id==IID_IDirectInput8A){*p=this;AddRef();return S_OK;}return E_NOINTERFACE;}
 ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs);}
 ULONG STDMETHODCALLTYPE Release() override{ULONG r=InterlockedDecrement(&refs);if(!r){real->Release();delete this;}return r;}
 HRESULT STDMETHODCALLTYPE CreateDevice(REFGUID g,LPDIRECTINPUTDEVICE8A* p,LPUNKNOWN u) override {
  if(!p)return E_POINTER;*p=nullptr;if(u)return CLASS_E_NOAGGREGATION;
  if(!nativePads&&(g==padGuid||g==pad2Guid)){*p=new Device(nullptr,false,g==pad2Guid?1:0);logf("CreateDevice virtual player=%d",g==pad2Guid?2:1);return DI_OK;}
  IDirectInputDevice8A* r=nullptr;HRESULT hr=real->CreateDevice(g,&r,u);
  if(SUCCEEDED(hr)&&r){*p=nativePads&&g!=GUID_SysKeyboard?r:new Device(r,g==GUID_SysKeyboard);logf("CreateDevice real keyboard=%d",g==GUID_SysKeyboard);}return hr;
 }
 HRESULT STDMETHODCALLTYPE EnumDevices(DWORD t,LPDIENUMDEVICESCALLBACKA cb,LPVOID ctx,DWORD flags) override {
  if(!cb)return DIERR_INVALIDPARAM;logf("EnumDevices type=%lu flags=%lu",t,flags);
  if(nativePads)return real->EnumDevices(t,cb,ctx,flags);
  auto pads=[&](){auto i=instance();if(cb(&i,ctx)==DIENUM_STOP)return;sample();sampleSecond();if(secondSlot>=0&&secondSlot!=selectedSlot){i=instance(1);cb(&i,ctx);}};
  if(t==DI8DEVCLASS_GAMECTRL||t==DI8DEVTYPE_GAMEPAD||t==DI8DEVTYPE_JOYSTICK){pads();return DI_OK;}
  if(t==DI8DEVCLASS_ALL){EnumContext c={cb,ctx};HRESULT hr=real->EnumDevices(t,enumOther,&c,flags);if(SUCCEEDED(hr)&&!c.stopped)pads();return hr;}
  return real->EnumDevices(t,cb,ctx,flags);
 }
 HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID g) override{return !nativePads&&(g==padGuid||g==pad2Guid)?DI_OK:real->GetDeviceStatus(g);}
 HRESULT STDMETHODCALLTYPE RunControlPanel(HWND w,DWORD f) override{return real->RunControlPanel(w,f);}
 HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE h,DWORD v) override{return real->Initialize(h,v);}
 HRESULT STDMETHODCALLTYPE FindDevice(REFGUID g,LPCSTR n,LPGUID out) override {if(n&&!strcmp(n,padName)){if(!out)return E_POINTER;*out=padGuid;return DI_OK;}return real->FindDevice(g,n,out);}
 HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(LPCSTR,LPDIACTIONFORMATA,LPDIENUMDEVICESBYSEMANTICSCBA,LPVOID,DWORD) override{return DIERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK,LPDICONFIGUREDEVICESPARAMSA,DWORD,LPVOID) override{return DIERR_UNSUPPORTED;}
};
#ifdef PADFIX_PLATFORM_ONLY
static std::unordered_map<void*,ULONGLONG> nativeKeyboardSent;
static HRESULT WINAPI platformKeyboardState(void* device,DWORD size,void* data){
 auto fn=(HRESULT(WINAPI*)(void*,DWORD,void*))originalMethod(device,9);if(!fn)return DIERR_INVALIDPARAM;
 HRESULT hr=fn(device,size,data);if(SUCCEEDED(hr)&&data&&size==256){std::lock_guard<std::recursive_mutex> lock(mutex);if(nativeKeyboardSent.count(device)){sample();pollControllerMenu();mergeMenuKeys((BYTE*)data);}}return hr;
}
static HRESULT WINAPI platformKeyboardData(void* device,DWORD stride,DIDEVICEOBJECTDATA* data,DWORD* count,DWORD flags){
 auto fn=(HRESULT(WINAPI*)(void*,DWORD,DIDEVICEOBJECTDATA*,DWORD*,DWORD))originalMethod(device,10);if(!fn)return DIERR_INVALIDPARAM;
 DWORD capacity=count?*count:0;HRESULT hr=fn(device,stride,data,count,flags);
 if(SUCCEEDED(hr)&&count&&stride==sizeof(DIDEVICEOBJECTDATA)){std::lock_guard<std::recursive_mutex> lock(mutex);auto it=nativeKeyboardSent.find(device);if(it!=nativeKeyboardSent.end()){sample();pollControllerMenu();filterMenuEvents(data,count);appendMenuEvents(data,count,capacity,flags,it->second);}}return hr;
}
static HRESULT WINAPI platformDISetCooperativeLevel(void* device,HWND window,DWORD flags) {
 auto fn=(HRESULT(WINAPI*)(void*,HWND,DWORD))originalMethod(device,13);if(!fn)return DIERR_INVALIDPARAM;
 DWORD actual=flags;if(sharedInput)actual=(flags&~(DISCL_EXCLUSIVE|DISCL_NOWINKEY))|DISCL_NONEXCLUSIVE;
 HRESULT hr=fn(device,window,actual);logf("DirectInput SetCooperativeLevel requested=%08lx actual=%08lx hr=%08lx",flags,actual,hr);return hr;
}
static HRESULT WINAPI platformDICreateDevice(void* input,REFGUID guid,void** output,LPUNKNOWN outer) {
 auto fn=(HRESULT(WINAPI*)(void*,REFGUID,void**,LPUNKNOWN))originalMethod(input,3);if(!fn)return DIERR_INVALIDPARAM;
 HRESULT hr=fn(input,guid,output,outer);
 if(SUCCEEDED(hr)&&output&&*output){
  if(sharedInput)hookMethod(*output,13,(void*)platformDISetCooperativeLevel);
  if(controllerTakeover&&guid==GUID_SysKeyboard){std::lock_guard<std::recursive_mutex> lock(mutex);nativeKeyboardSent[*output]=menuSerial;hookMethod(*output,9,(void*)platformKeyboardState);hookMethod(*output,10,(void*)platformKeyboardData);}
 }return hr;
}
#endif
extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE h,DWORD v,REFIID id,void** p,LPUNKNOWN u) {
 static std::once_flag once;static HRESULT (WINAPI* create)(HINSTANCE,DWORD,REFIID,void**,LPUNKNOWN);
 std::call_once(once,[]{initializeAll();HMODULE d=systemDll("dinput8.dll");if(d)create=(decltype(create))GetProcAddress(d,"DirectInput8Create");});
 startControllerWorker();
 if(!create)return E_FAIL;if(!p)return E_POINTER;*p=nullptr;
#ifdef PADFIX_PLATFORM_ONLY
 if(!_stricmp(gameExeName,"Launch.exe")&&controllerTakeover&&id==IID_IDirectInput8A){IDirectInput8A* r=nullptr;HRESULT hr=create(h,v,id,(void**)&r,u);if(SUCCEEDED(hr)&&r)*p=new Input(r,true);return hr;}
 HRESULT hr=create(h,v,id,p,u);
 if((sharedInput||controllerTakeover)&&SUCCEEDED(hr)&&p&&*p&&(id==IID_IDirectInput8A||id==IID_IDirectInput8W)){hookMethod(*p,3,(void*)platformDICreateDevice);logf("DirectInput interface hooked for keyboard ownership/nonexclusive devices");}
 return hr;
#else
 if(!enabled||id!=IID_IDirectInput8A)return create(h,v,id,p,u);
 IDirectInput8A* r=nullptr;HRESULT hr=create(h,v,id,(void**)&r,u);if(SUCCEEDED(hr)&&r){*p=new Input(r);logf("DirectInput8Create ANSI wrapped");}return hr;
#endif
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID) {if(reason==DLL_PROCESS_ATTACH){selfModule=h;installEarlyHooks();}return TRUE;}
