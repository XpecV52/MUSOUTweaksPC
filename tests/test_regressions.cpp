#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <vector>
static ULONGLONG ticks=1000;
static std::vector<INPUT> injected;
static bool heldKeys[256]={};
static ULONGLONG WINAPI mockTicks(){return ticks;}
static UINT WINAPI mockInput(UINT n,LPINPUT p,int){for(UINT i=0;i<n;i++)injected.push_back(p[i]);return n;}
static SHORT WINAPI mockKey(int key){return (SHORT)(heldKeys[key]?0x8000:0);}
#define GetTickCount64 mockTicks
#define SendInput mockInput
#define GetAsyncKeyState mockKey
#define PADFIX_TEST
#include "../src/padfix.cpp"
#include <cstdlib>
static XINPUT_STATE states[4]={};static bool online[4]={true,true,false,false};
static XINPUT_VIBRATION motors[4]={};
static DWORD WINAPI fakeGet(DWORD slot,XINPUT_STATE* p){if(slot>=4||!online[slot])return ERROR_DEVICE_NOT_CONNECTED;*p=states[slot];return ERROR_SUCCESS;}
static DWORD WINAPI fakeSet(DWORD slot,XINPUT_VIBRATION* p){if(slot>=4||!online[slot])return ERROR_DEVICE_NOT_CONNECTED;motors[slot]=*p;return ERROR_SUCCESS;}
static void WINAPI clearSsm7RumbleQueue(BYTE* pad){memset(pad+0xb8,0,10);}
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
  check((WORD)word(0,1)&profile->confirm,"1P controller confirm works before 2P joins");
  check(second(1)==0xffff,"no co-op routing before player two joins");
  splitCharMode=2;
  auto* join=(DWORD*)(splitBase+profile->characterJoinPrompt);
  join[0]=1;join[1]=0;
  check(!nativeSplitJoined(),"pending dual-player selection preserves native join shortcut");
  splitCharMode=1;splitCharP1Ready=true;
  check(nativeSplitJoined(),"player two reselection uses the existing player one selection");
  splitCharMode=2;splitCharP1Ready=false;
  join[1]=1;
  check(nativeSplitJoined()&&!splitCharP1Ready,"2P joins before the native selector marks 1P ready");
  splitCharMode=-1;
  *(DWORD*)(splitBase+profile->joinedFlags)=0x100;
  word(1,2)=0xffffffff;
  applySplitMenuActions(false,false);
 check(second(1)==0xffff&&second(2)==0xffff,"common menus preserve player two binding actions");
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
  splitCharP1Ready=false;splitCharMode=-1;splitSecondJoined=false;splitJoinedBase=nullptr;
  InterlockedExchange(&secondMenuKeyboardPassthrough,0);
 }
 splitProfile=nullptr;splitBase=nullptr;splitSourcesReady=false;
 heldKeys[VK_RETURN]=heldKeys[VK_ESCAPE]=true;
 menuKeys[0]=menuKeys[1]=true;
 menuKeyMode=3;
 check(!splitKeyboardState(0x80,0x40),"pending 1P menu key pulses do not become 2P actions");
 heldKeys['L']=heldKeys['K']=true;
 check(splitKeyboardState(0x80,0x40)==0xc0,"physical L and K still reach 2P");
 heldKeys['L']=heldKeys['K']=heldKeys[VK_RETURN]=heldKeys[VK_ESCAPE]=false;
 menuKeys[0]=menuKeys[1]=false;
 {
  std::vector<BYTE> image(0xba6828);
  sw2InputBase=image.data();
  originalSw2Poll=[]{};
  const DWORD base=(DWORD)(uintptr_t)sw2InputBase;
  auto& update=*(DWORD*)(sw2InputBase+0x36ced0);
  auto& joined=*(DWORD*)(sw2InputBase+0x3a9628);
  auto& readyCursor=*(DWORD*)(sw2InputBase+0x4854fc);
  auto* firstPlayer=(DWORD*)(sw2InputBase+0xba6728);
  auto* secondPlayer=(DWORD*)(sw2InputBase+0xba6818);
  update=base+0x84360;
  readSw2Input();
  heldKeys['L']=true;readSw2Input();
  check(!secondPlayer[2],"SW2 blocks keyboard confirmation before 2P joins");
  joined=2;
  heldKeys['L']=false;heldKeys['K']=true;readSw2Input();
  check(!secondPlayer[2],"SW2 blocks keyboard back before 1P is ready");
  readyCursor=1;readSw2Input();
  check(!secondPlayer[2],"SW2 ignores a back key held while readiness changes");
  heldKeys['K']=false;readSw2Input();
  heldKeys['W']=heldKeys['L']=true;states[0].Gamepad.wButtons=XINPUT_GAMEPAD_DPAD_RIGHT;
  readSw2Input();
  check(firstPlayer[2]==8&&secondPlayer[2]==(1|0x80),"SW2 separates 1P D-pad from 2P WASD and L");
  heldKeys['W']=heldKeys['L']=false;states[0]={};readSw2Input();
  heldKeys['K']=true;readSw2Input();
  check(secondPlayer[2]==0x40&&firstPlayer[2]==0,"SW2 K backs out of only the 2P cursor");
  heldKeys['K']=false;
  update=base+0x7fef0;
  auto& shopOwner=*(DWORD*)(sw2InputBase+0x484630);
  readSw2Input();
  heldKeys['D']=heldKeys['L']=true;readSw2Input();
  check(!secondPlayer[2],"SW2 shop keeps 2P keys gated while 1P shops");
  shopOwner=1;readSw2Input();
  check(!secondPlayer[2],"SW2 shop handoff ignores already held keys");
  heldKeys['D']=heldKeys['L']=false;readSw2Input();
  heldKeys['D']=heldKeys['L']=true;readSw2Input();
  check(secondPlayer[2]==(8|0x80)&&!firstPlayer[2],"SW2 shop hands movement and confirm to 2P after handoff");
  heldKeys['D']=heldKeys['L']=false;readSw2Input();
  heldKeys[VK_ESCAPE]=true;readSw2Input();
  check(secondPlayer[2]==0x40&&!firstPlayer[2],"SW2 shop back reaches 2P only");
  heldKeys[VK_ESCAPE]=false;
  // StateShop.cpp uses a separate owner field and can hand the page to P2
  // before the global join byte is updated.
  update=base+0x8adc0;
  joined=0;
  auto& stateShopOwner=*(DWORD*)(sw2InputBase+0x485e20);
  stateShopOwner=0;readSw2Input();
  heldKeys['D']=heldKeys['L']=true;readSw2Input();
  check(!secondPlayer[2],"SW2 StateShop keeps P2 gated while P1 owns the page");
  heldKeys['D']=heldKeys['L']=false;readSw2Input();
  stateShopOwner=1;readSw2Input();
  heldKeys['D']=heldKeys['L']=true;readSw2Input();
  check(secondPlayer[2]==(8|0x80)&&!firstPlayer[2],"SW2 StateShop hands movement and confirm to P2 by owner");
  heldKeys['D']=heldKeys['L']=false;
  // The preparation child is dispatched independently from the parent update.
  update=base+0x7daf0;
  joined=0;
  readyCursor=1;*(DWORD*)(sw2InputBase+0x4845e0)=1;readSw2Input();
  heldKeys['W']=heldKeys['L']=true;readSw2Input();
  check(secondPlayer[2]==(1|0x80)&&!firstPlayer[2],"SW2 preparation child routes 2P keyboard to its owner");
  heldKeys['W']=heldKeys['L']=false;
  update=0;readSw2Input();
  sw2InputBase=nullptr;originalSw2Poll=nullptr;
 }
 nativeSplitMenuActive=1;
 BYTE nativeKeyboard[256]={};nativeKeyboard[DIK_S]=nativeKeyboard[DIK_F1]=nativeKeyboard[DIK_K]=nativeKeyboard[DIK_L]=0x80;
 mergeMenuKeys(nativeKeyboard);
 check(!nativeKeyboard[DIK_S]&&!nativeKeyboard[DIK_K]&&!nativeKeyboard[DIK_L]&&nativeKeyboard[DIK_F1],
       "polled keyboard isolates 2P movement and K/L while preserving native Join");
 DIDEVICEOBJECTDATA sharedEvents[4]={};
 sharedEvents[0].dwOfs=DIK_W;sharedEvents[1].dwOfs=DIK_F12;
 sharedEvents[2].dwOfs=DIK_K;sharedEvents[3].dwOfs=DIK_L;
 DWORD sharedCount=4;filterMenuEvents(sharedEvents,&sharedCount);
 check(sharedCount==1&&sharedEvents[0].dwOfs==DIK_F12,"buffered keyboard isolates 2P actions and keeps Join");
 nativeSplitMenuActive=0;
 // Disabling the takeover switch must leave the game's original shared
 // keyboard and second-controller paths untouched.
 controllerTakeover=0;
 menuSceneActive=1;
 nativeSplitMenuActive=1;
 nativeSecondMenuActive=0;
 menuKeys[0]=menuKeys[1]=true;
 BYTE originalMenuKeys[256]={};originalMenuKeys[DIK_RETURN]=originalMenuKeys[DIK_ESCAPE]=0x80;
 mergeMenuKeys(originalMenuKeys);
 check(originalMenuKeys[DIK_RETURN]&&originalMenuKeys[DIK_ESCAPE],"takeover off keeps physical 1P Enter/Esc");
 DIDEVICEOBJECTDATA originalMenuEvents[2]={};DWORD originalMenuCount=2;
 originalMenuEvents[0].dwOfs=DIK_K;originalMenuEvents[1].dwOfs=DIK_L;
 filterMenuEvents(originalMenuEvents,&originalMenuCount);
 check(originalMenuCount==2,"takeover off keeps 2P keyboard menu events");
 states[1]={};sampleSecond();states[1].Gamepad.wButtons=XINPUT_GAMEPAD_B;
 check(sampleSecond().buttons[1],"takeover off keeps 2P controller menu buttons");
 states[1]={};sampleSecond();
 controllerTakeover=1;
 menuKeys[0]=menuKeys[1]=false;
 nativeSplitMenuActive=0;
 check(ssm7RumbleEnumerationFlags(DI8DEVCLASS_GAMECTRL,DIEDFL_ATTACHEDONLY)==DIEDFL_ATTACHEDONLY&&
       ssm7RumbleEnumerationFlags(DI8DEVCLASS_GAMECTRL,DIEDFL_ATTACHEDONLY|DIEDFL_FORCEFEEDBACK)==DIEDFL_ATTACHEDONLY&&
       ssm7RumbleEnumerationFlags(DI8DEVCLASS_KEYBOARD,DIEDFL_FORCEFEEDBACK)==DIEDFL_FORCEFEEDBACK,
       "SSM7 force feedback enumeration extends game controller discovery only");
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
 strcpy_s(gameExeName,"SM5_WIN.exe");
 first=sample();check(first.buttons[1],"SM5 binding pages keep physical B visible");
 states[0].Gamepad.wButtons=XINPUT_GAMEPAD_DPAD_RIGHT;
 first=sample();check(first.dpad==XINPUT_GAMEPAD_DPAD_RIGHT&&first.buttons[15],"SM5 binding pages keep physical D-pad visible");
 {
  // SM5 has separate held, pressed and repeated action words. A raw pad
  // snapshot must not be copied into all three words, or one tap skips two
  // menu entries and a held stick is treated as a fresh press every poll.
  std::vector<BYTE> image(0x35cf40+0x140*2+0x20);
  ssm5InputBase=image.data();
  ssm5PadKeys={};
  auto* p1=(DWORD*)(ssm5InputBase+ssm5PlayerStateRva);
  ssm5PadKeys.update(8,1000);
  writeSsm5MenuInput(0,ssm5PadKeys,ssm5MenuMask|ssm5InputX|ssm5InputY,false);
  check((p1[2]&0x4000)&&(p1[6]&0x4000)&&(p1[8]&0x4000),"SM5 D-pad tap emits one initial edge");
  ssm5PadKeys.update(8,1010);
  writeSsm5MenuInput(0,ssm5PadKeys,ssm5MenuMask|ssm5InputX|ssm5InputY,false);
  check(!(p1[2]&0x4000)&&!(p1[6]&0x4000)&&!(p1[8]&0x4000),"SM5 held D-pad does not duplicate a tap");
  ssm5PadKeys.update(8,1400);
  writeSsm5MenuInput(0,ssm5PadKeys,ssm5MenuMask|ssm5InputX|ssm5InputY,false);
  check((p1[2]&0x4000)&&(p1[8]&0x4000),"SM5 held D-pad repeats only after the delay");
  ssm5PadKeys={};
  ssm5PadKeys.update(ssm5InputX,2000);
  writeSsm5MenuInput(0,ssm5PadKeys,ssm5MenuMask|ssm5InputX|ssm5InputY,false);
  check((p1[2]&ssm5InputX)&&(p1[6]&ssm5InputX),"SM5 X action has an initial edge");
  ssm5PadKeys.update(ssm5InputX,2010);
  writeSsm5MenuInput(0,ssm5PadKeys,ssm5MenuMask|ssm5InputX|ssm5InputY,false);
  check((p1[2]&ssm5InputX)&&!(p1[6]&ssm5InputX),"SM5 held X is not re-pressed every poll");
  ssm5InputBase=nullptr;
 }
 strcpy_s(gameExeName,"");
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
 nativeSecondMenuActive=1;
 check(sampleSecond().buttons[1],"2P second controller remains available in its active menu cursor");
 nativeSecondMenuActive=0;
 menuSceneActive=0;check(sampleSecond().buttons[1],"2P battle buttons available");
 states[0].Gamepad.wButtons=XINPUT_GAMEPAD_A;first=sample();check(first.buttons[0],"battle A preserved");
 strcpy_s(gameExeName,"OROCHI_Z_TC.exe");
 menuSceneActive=1;first=sample();check(first.buttons[0],"Orochi menu keeps 1P B/A available to native menu routing");
 strcpy_s(gameExeName,"");
 online[0]=false;lastConnection=0;ticks+=2000;first=sample();check(selectedSlot==0&&!first.buttons[0],"1P disconnect never promotes 2P");
 online[0]=true;states[0]={};sample();sample();
 Device pad;DIDEVCAPS caps={sizeof(caps)};pad.GetCapabilities(&caps);check((caps.dwFlags&DIDC_FORCEFEEDBACK)!=0,"force feedback advertised");
 DICONSTANTFORCE force={5000};DIEFFECT desc={};desc.dwSize=sizeof(desc);desc.dwDuration=100000;desc.dwGain=10000;desc.cbTypeSpecificParams=sizeof(force);desc.lpvTypeSpecificParams=&force;
 IDirectInputEffect* effect=nullptr;check(pad.CreateEffect(GUID_ConstantForce,&desc,&effect,nullptr)==DI_OK&&effect,"create constant effect");
 check(effect->SetParameters(&desc,DIEP_START|DIEP_DURATION|DIEP_TYPESPECIFICPARAMS|DIEP_DIRECTION)==DI_OK
  &&motors[0].wLeftMotorSpeed>32000&&!motors[1].wLeftMotorSpeed,"game SetParameters starts effect on assigned player");
 ticks+=101;updateRumble();check(!motors[0].wLeftMotorSpeed,"finite effect timer stops motor");
 effect->Start(INFINITE,0);pad.SendForceFeedbackCommand(DISFFC_PAUSE);check(!motors[0].wLeftMotorSpeed,"pause silences motor");
 pad.SendForceFeedbackCommand(DISFFC_CONTINUE);check(motors[0].wLeftMotorSpeed,"continue resumes effect");
 pad.SendForceFeedbackCommand(DISFFC_STOPALL);check(!motors[0].wLeftMotorSpeed,"stop-all silences motor");
 effect->Start(INFINITE,0);online[0]=false;updateRumble();DWORD status=99;effect->GetEffectStatus(&status);check(!status,"disconnect stops effect");
 online[0]=true;stopRumble(0);effect->Release();check(padEffects.empty(),"effect COM lifetime cleaned");
 {
  std::vector<BYTE> image(0x6afe38);
  BYTE padState[0xc8]={},settings[0x41]={};
  ssm7RumbleBase=image.data();
  originalSsm7PadUpdate=clearSsm7RumbleQueue;
  *(BYTE**)(ssm7RumbleBase+0x6aef68)=settings;
  *(BYTE**)(ssm7RumbleBase+0x6af1c4)=padState;
  auto& directInputPads=*(DWORD*)(ssm7RumbleBase+0x6afe34);
  BYTE deviceData[0x1b1]={};
  BYTE* devices[]={nullptr,deviceData};
  *(BYTE***)(ssm7RumbleBase+0x6afe2c)=devices;
  const auto queuePreview=[&]{
   *(DWORD*)(padState+0xb8)=*(DWORD*)(padState+0xbc)=12;
   padState[0xc0]=1;padState[0xc1]=128;
   readSsm7RumbleQueue(padState);
  };
  queuePreview();
  check(motors[0].wLeftMotorSpeed==128*257&&motors[0].wRightMotorSpeed==65535&&
        !motors[1].wRightMotorSpeed,"SSM7 uses the native queue for both motors of the first Steam Input pad");
  ticks+=201;updateRumble();
  check(!motors[0].wLeftMotorSpeed&&!motors[0].wRightMotorSpeed,"SSM7 XInput fallback stops after native preview duration");
  settings[0x40]=1;queuePreview();
  check(!motors[0].wRightMotorSpeed,"SSM7 fallback respects the native vibration setting");
  settings[0x40]=0;directInputPads=1;deviceData[0x1b0]=1;queuePreview();
  check(!motors[0].wRightMotorSpeed,"SSM7 fallback leaves native DirectInput feedback in control");
  deviceData[0x1b0]=0;queuePreview();
  check(motors[0].wRightMotorSpeed,"SSM7 fallback covers a Steam controller without a native effect");
  ticks+=201;updateRumble();
  ssm7RumbleBase=nullptr;
  originalSsm7PadUpdate=nullptr;
 }
 desc.dwSize=offsetof(DIEFFECT,dwStartDelay);
 check(pad.CreateEffect(GUID_ConstantForce,&desc,&effect,nullptr)==DI_OK&&effect,"legacy DirectInput effect structure accepted");
 effect->Release();
 BYTE usbReport[32]={};
 ds4MotorReport(usbReport,sizeof(usbReport),0x1200,0xab00);
 check(usbReport[0]==5&&usbReport[1]==1&&usbReport[4]==0xab&&usbReport[5]==0x12,"DS4 USB motor report");
 BYTE btReport[78]={};
 ds4MotorReport(btReport,sizeof(btReport),0x1200,0xab00);
 const BYTE crc[]={0xe2,0xf8,0xbc,0xff};
 check(btReport[0]==0x11&&btReport[1]==0xc0&&btReport[3]==1&&btReport[6]==0xab&&btReport[7]==0x12
  &&memcmp(btReport+74,crc,sizeof(crc))==0,"DS4 Bluetooth motor report with CRC");
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
