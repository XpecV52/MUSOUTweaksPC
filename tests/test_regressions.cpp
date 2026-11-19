#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <vector>
static ULONGLONG ticks=1000;
static std::vector<INPUT> injected;
static ULONGLONG WINAPI mockTicks(){return ticks;}
static UINT WINAPI mockInput(UINT n,LPINPUT p,int){for(UINT i=0;i<n;i++)injected.push_back(p[i]);return n;}
#define GetTickCount64 mockTicks
#define SendInput mockInput
#define PADFIX_TEST
#include "../src/padfix.cpp"
#include <cstdlib>
static XINPUT_STATE states[4]={};static bool online[4]={true,true,false,false};
static XINPUT_VIBRATION motors[4]={};
static DWORD WINAPI fakeGet(DWORD slot,XINPUT_STATE* p){if(slot>=4||!online[slot])return ERROR_DEVICE_NOT_CONNECTED;*p=states[slot];return ERROR_SUCCESS;}
static DWORD WINAPI fakeSet(DWORD slot,XINPUT_VIBRATION* p){if(slot>=4||!online[slot])return ERROR_DEVICE_NOT_CONNECTED;motors[slot]=*p;return ERROR_SUCCESS;}
static int checks=0;
static void check(bool v,const char* m){++checks;if(!v){fprintf(stderr,"FAIL: %s\n",m);exit(1);}}
int main(){
 foregroundOnly=0;neutralMs=0;getXState=fakeGet;setXState=fakeSet;controllerTakeover=1;
 sample();sample();check(selectedSlot==0&&armed,"1P pinned to slot 0 and armed");
 // Both engine layouts must replace the duplicated 2P native action bits,
 // including the repeat word used for cursor navigation.
 for(const SplitMenuProfile* profile:{&splitSsm3,&splitSsm4}){
  std::vector<BYTE> image(0x640000);
  splitBase=image.data();splitProfile=profile;splitSourcesReady=true;
  auto word=[&](unsigned player,unsigned kind)->DWORD&{
   DWORD base=player?profile->player2Actions:profile->player1Actions;
   unsigned offset=profile->packedActions?kind:(kind?kind+1:0);
   return ((DWORD*)(splitBase+base))[offset];
  };
  const unsigned secondShift=profile->packedActions?16:0;
  const auto second=[&](unsigned kind){return WORD(word(1,kind)>>secondShift);};
  splitPadKeys.update(4|profile->confirm,1000);
  splitKeyboardKeys.update(2|profile->cancel,1000);
  word(1,1)=0xffffffff;
  applySplitMenuActions(true,true);
  check(second(1)==0xffff,"no co-op routing before player two joins");
  splitCharMode=2;
  auto* join=(DWORD*)(splitBase+profile->characterJoinPrompt);
  join[0]=1;join[1]=0;
  check(!nativeSplitJoined(),"pending dual-player selection preserves native join shortcut");
  join[1]=1;
  check(nativeSplitJoined()&&!splitCharP1Ready,"2P joins before the native selector marks 1P ready");
  splitCharMode=-1;
  *(DWORD*)(splitBase+profile->joinedFlags)=0x100;
  applySplitMenuActions(false,false);
  check(!second(1)&&!second(2),"common menus accept no player two input even when joined");
  applySplitMenuActions(true,false);
  check((WORD)word(0,1)==(WORD)((0xffff&~(15|profile->confirm|profile->cancel))|4|profile->confirm)||
        (!profile->packedActions&&(WORD)word(0,1)==(4|profile->confirm)),"1P keeps its controller direction and B confirm");
  check(second(1)==2&&second(2)==2,"2P uses only keyboard direction while 1P is unready");
  applySplitMenuActions(true,true);
  check(second(1)==(2|profile->cancel)&&second(2)==(2|profile->cancel),"2P Escape reaches its native action word after 1P is ready");
  splitKeyboardKeys.update(2|profile->cancel,1410);
  applySplitMenuActions(true,true);
  check(second(1)==0&&second(2)==2,"held cancel does not repeat while direction does");
  splitPadKeys={};splitKeyboardKeys={};
 }
 splitProfile=nullptr;splitBase=nullptr;splitSourcesReady=false;
 nativeSplitMenuActive=1;
 BYTE nativeKeyboard[256]={};nativeKeyboard[DIK_S]=nativeKeyboard[DIK_F1]=0x80;
 mergeMenuKeys(nativeKeyboard);
 check(!nativeKeyboard[DIK_S]&&nativeKeyboard[DIK_F1],"polled keyboard isolates 2P movement while preserving native Join");
 DIDEVICEOBJECTDATA sharedEvents[2]={};sharedEvents[0].dwOfs=DIK_W;sharedEvents[1].dwOfs=DIK_F12;
 DWORD sharedCount=2;filterMenuEvents(sharedEvents,&sharedCount);
 check(sharedCount==1&&sharedEvents[0].dwOfs==DIK_F12,"buffered keyboard isolates 2P movement and keeps Join");
 nativeSplitMenuActive=0;
 // A 150 ms tap used to emit two key-downs. Repeat starts after 400 ms.
 dpadArrowKeys=2;menuInjectOnly=1;menuSceneActive=1;dpadRepeatDelayMs=400;
 lastDpadBits=XINPUT_GAMEPAD_DPAD_DOWN;maybeSendArrowKeys();
 ticks+=41;maybeSendArrowKeys();ticks+=100;maybeSendArrowKeys();
 check(injected.size()==2&&!(injected[0].ki.dwFlags&KEYEVENTF_KEYUP)&&(injected[1].ki.dwFlags&KEYEVENTF_KEYUP),"150ms tap sends one down/up pair");
 ticks+=260;maybeSendArrowKeys();check(injected.size()==3,"long hold repeats after delay");
 lastDpadBits=0;maybeSendArrowKeys();check(!arrowInputDown&&!arrowInputHeld,"release cancels repeat state");dpadArrowKeys=0;
 padAxes=6;triggerAxes=0;states[0].Gamepad.bLeftTrigger=255;auto first=sample();
 check(first.buttons[10]&&first.z==-255&&first.rz==-255,"LT bindable with duplicate axes at rest");states[0]={};sample();
 menuKeyMode=1;states[0].Gamepad.wButtons=XINPUT_GAMEPAD_B;first=sample();pollControllerMenu();
 check(!first.buttons[1]&&menuKeys[0],"menu B owns Enter without native duplicate");
 BYTE keys[256]={};keys[DIK_ESCAPE]=keys[DIK_RETURN]=0x80;mergeMenuKeys(keys);
 check(keys[DIK_RETURN]==0x80&&!keys[DIK_ESCAPE],"physical Enter/Esc replaced with 1P state");
 DIDEVICEOBJECTDATA events[8]={};DWORD count=0;ULONGLONG cursor=0;
 appendMenuEvents(events,&count,8,DIGDD_PEEK,cursor);check(count==1&&!cursor,"peek preserves menu press");
 count=0;appendMenuEvents(events,&count,8,0,cursor);check(count==1&&cursor&&events[0].dwOfs==DIK_RETURN,"buffered menu press delivered");
 count=0;appendMenuEvents(events,&count,8,0,cursor);check(!count,"no duplicate buffered press");
 ticks+=61;pollControllerMenu();check(!menuKeys[0],"held B pulse releases without repeating");
 ULONGLONG slowCursor=0;count=0;appendMenuEvents(events,&count,8,0,slowCursor);
 check(count==2&&events[0].dwData==0x80&&events[1].dwData==0,"slow buffered reader retains a complete short tap");
 states[0]={};sample();pollControllerMenu();sampleSecond();states[1].Gamepad.wButtons=XINPUT_GAMEPAD_B;
 sampleSecond();check(lastPadButtons==0&&!menuKeys[0],"2P cannot synthesize 1P Enter");
 check(!sampleSecond().buttons[1],"2P menu controls suppressed");
 menuSceneActive=0;check(sampleSecond().buttons[1],"2P battle buttons available");
 states[0].Gamepad.wButtons=XINPUT_GAMEPAD_A;first=sample();check(first.buttons[0],"battle A preserved");
 online[0]=false;lastConnection=0;ticks+=2000;first=sample();check(selectedSlot==0&&!first.buttons[0],"1P disconnect never promotes 2P");
 online[0]=true;states[0]={};sample();sample();
 Device pad;DIDEVCAPS caps={sizeof(caps)};pad.GetCapabilities(&caps);check((caps.dwFlags&DIDC_FORCEFEEDBACK)!=0,"force feedback advertised");
 DICONSTANTFORCE force={5000};DIEFFECT desc={};desc.dwSize=sizeof(desc);desc.dwDuration=100000;desc.dwGain=10000;desc.cbTypeSpecificParams=sizeof(force);desc.lpvTypeSpecificParams=&force;
 IDirectInputEffect* effect=nullptr;check(pad.CreateEffect(GUID_ConstantForce,&desc,&effect,nullptr)==DI_OK&&effect,"create constant effect");
 check(effect->Start(1,0)==DI_OK&&motors[0].wLeftMotorSpeed>32000&&!motors[1].wLeftMotorSpeed,"effect routed to assigned player");
 ticks+=101;updateRumble();check(!motors[0].wLeftMotorSpeed,"finite effect timer stops motor");
 effect->Start(INFINITE,0);pad.SendForceFeedbackCommand(DISFFC_PAUSE);check(!motors[0].wLeftMotorSpeed,"pause silences motor");
 pad.SendForceFeedbackCommand(DISFFC_CONTINUE);check(motors[0].wLeftMotorSpeed,"continue resumes effect");
 pad.SendForceFeedbackCommand(DISFFC_STOPALL);check(!motors[0].wLeftMotorSpeed,"stop-all silences motor");
 effect->Start(INFINITE,0);online[0]=false;updateRumble();DWORD status=99;effect->GetEffectStatus(&status);check(!status,"disconnect stops effect");
 online[0]=true;stopRumble(0);effect->Release();check(padEffects.empty(),"effect COM lifetime cleaned");
 // Exercise the actual production keyboard wrapper, not just merge helpers.
 // An OS-injected key used to be cleared by the wrapper while A/B were also
 // suppressed on the pad, leaving neither channel able to confirm or cancel.
 check(menuKeyModeForExecutable("ShinSangokumusou3.exe")==1,"SSM3 menu uses its DirectInput keyboard");
 check(menuKeyModeForExecutable("SM4_WIN.exe")==1,"SSM4 menu uses its DirectInput keyboard");
 check(menuKeyModeForExecutable("OROCHI_Z_TC.exe")==3&&menuKeyModeForExecutable("SW2.exe")==3,"message-only ports retain OS delivery");
 HMODULE inputModule=systemDll("dinput8.dll");
 auto create=(HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,void**,LPUNKNOWN))GetProcAddress(inputModule,"DirectInput8Create");
 IDirectInput8A* systemInput=nullptr;
 check(create(GetModuleHandleA(nullptr),0x800,IID_IDirectInput8A,(void**)&systemInput,nullptr)==DI_OK,"real keyboard integration factory");
 HWND keyboardWindow=CreateWindowExA(0,"STATIC","Menu takeover regression",0,0,0,1,1,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);
 check(keyboardWindow!=nullptr,"real keyboard integration window");
 menuSceneActive=1;states[0]={};sample();pollControllerMenu();
 for(int mode:{menuKeyModeForExecutable("ShinSangokumusou3.exe"),3}){
  menuKeyMode=mode;
  IDirectInputDevice8A* systemKeyboard=nullptr;
  check(systemInput->CreateDevice(GUID_SysKeyboard,&systemKeyboard,nullptr)==DI_OK,"real keyboard integration device");
  Device keyboard(systemKeyboard,true);
  check(keyboard.SetDataFormat(&c_dfDIKeyboard)==DI_OK,"real keyboard integration format");
  check(keyboard.SetCooperativeLevel(keyboardWindow,DISCL_BACKGROUND|DISCL_NONEXCLUSIVE)==DI_OK,"real keyboard integration cooperative level");
  DIPROPDWORD buffering={{sizeof(buffering),sizeof(DIPROPHEADER),0,DIPH_DEVICE},32};
  check(keyboard.SetProperty(DIPROP_BUFFERSIZE,&buffering.diph)==DI_OK&&keyboard.Acquire()==DI_OK,"real keyboard integration acquired");
  for(unsigned k=0;k<2;k++){
   const BYTE dik=k?DIK_ESCAPE:DIK_RETURN;
   states[0].Gamepad.wButtons=k?XINPUT_GAMEPAD_A:XINPUT_GAMEPAD_B;
   BYTE actualKeys[256]={};
   check(keyboard.GetDeviceState(256,actualKeys)==DI_OK&&(actualKeys[dik]&0x80),"1P B/A reaches real polled keyboard after physical-key filtering");
   count=8;check(keyboard.GetDeviceData(sizeof(events[0]),events,&count,DIGDD_PEEK)==DI_OK,"real buffered keyboard peek");
   bool downSeen=false;for(DWORD i=0;i<count;i++)if(events[i].dwOfs==dik&&events[i].dwData==0x80)downSeen=true;
   check(downSeen,"1P B/A reaches buffered keyboard after duplicate filtering");
   count=8;keyboard.GetDeviceData(sizeof(events[0]),events,&count,0);
   states[0]={};ticks+=61;
   check(keyboard.GetDeviceState(256,actualKeys)==DI_OK&&!(actualKeys[dik]&0x80),"real polled keyboard menu pulse releases");
   count=8;keyboard.GetDeviceData(sizeof(events[0]),events,&count,0);
   bool upSeen=false;for(DWORD i=0;i<count;i++)if(events[i].dwOfs==dik&&!events[i].dwData)upSeen=true;
   check(upSeen,"real buffered keyboard menu pulse releases");
  }
  keyboard.Unacquire();systemKeyboard->Release();
 }
 systemInput->Release();DestroyWindow(keyboardWindow);
 monitorBounds={10,20,1930,1100};WINDOWPOS pos={};pos.hwndInsertAfter=HWND_TOP;pos.flags=SWP_NOMOVE|SWP_NOSIZE;
 constrainBorderlessPosition(&pos);check(pos.hwndInsertAfter==HWND_TOP&&!(pos.flags&SWP_NOZORDER),"taskbar activation preserves requested Z-order");
 check(pos.x==10&&pos.y==20&&pos.cx==1920&&pos.cy==1080,"borderless monitor geometry retained");
 pos.flags=SWP_NOZORDER;pos.hwndInsertAfter=HWND_BOTTOM;constrainBorderlessPosition(&pos);check(pos.flags&SWP_NOZORDER,"NOZORDER preserved");
 pos.flags=0;pos.hwndInsertAfter=HWND_TOPMOST;constrainBorderlessPosition(&pos);check(pos.hwndInsertAfter==HWND_TOP,"topmost becomes ordinary top");
 printf("PASS: %d controller/window regression checks\n",checks);return 0;
}
