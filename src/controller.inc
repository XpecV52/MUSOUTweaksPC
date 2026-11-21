// Application-local menu ownership and DirectInput -> XInput force feedback.
static const ULONG_PTR controllerKeyTag=0x4D55534F;
static bool menuKeys[2]={},menuButtons[2]={};
static ULONGLONG menuKeyUntil[2]={};
struct MenuTransition{ULONGLONG serial;unsigned key;bool down;DWORD time;};
static ULONGLONG menuSerial=0;
static std::deque<MenuTransition> menuTransitions;
static bool controllerMenuActive(){
 std::lock_guard<std::recursive_mutex> lock(mutex);
 return controllerTakeover&&(menuSceneActive||nativeSplitMenuActive)&&armed&&wasConnected&&foreground();
}
static void sendMenuKey(unsigned k,bool down){
 // Keep an authoritative transition stream for every transport. A title can
 // expose a DirectInput keyboard even when its other screens use WM_KEY*;
 // filtering the duplicate OS event must not discard the controller's key.
 menuTransitions.push_back({++menuSerial,k,down,GetTickCount()});
 if(menuTransitions.size()>256)menuTransitions.pop_front();
 if(menuKeyMode==1)return;
 INPUT in={};in.type=INPUT_KEYBOARD;
 in.ki.wScan=(WORD)MapVirtualKeyA(k?VK_ESCAPE:VK_RETURN,MAPVK_VK_TO_VSC);
 in.ki.dwFlags=KEYEVENTF_SCANCODE|(down?0:KEYEVENTF_KEYUP);in.ki.dwExtraInfo=controllerKeyTag;
 SendInput(1,&in,sizeof(in));
}
static void pollControllerMenu(){
 std::lock_guard<std::recursive_mutex> lock(mutex);
 if(!controllerTakeover)return;
 // Native menu routers still need the controller's Enter/Esc pulse for pages
 // that bypass the action table, such as skip prompts and control settings.
 const bool active=controllerMenuActive();
 const WORD buttons=(WORD)lastPadButtons;
 const WORD masks[]={XINPUT_GAMEPAD_B,XINPUT_GAMEPAD_A};
 ULONGLONG now=GetTickCount64();
 for(unsigned k=0;k<2;k++){
  bool down=active&&(buttons&masks[k]);
  if(!active){if(menuKeys[k])sendMenuKey(k,false);menuKeys[k]=false;menuKeyUntil[k]=0;}
  else {
   if(down&&!menuButtons[k]&&!menuKeys[k]){menuKeys[k]=true;menuKeyUntil[k]=now+(ULONGLONG)enterPulseMs;sendMenuKey(k,true);}
   if(menuKeys[k]&&now>=menuKeyUntil[k]){menuKeys[k]=false;sendMenuKey(k,false);}
  }
  // Require a release across scene transitions, too.
  menuButtons[k]=(buttons&masks[k])!=0;
 }
}
static bool isSplitKeyboardKey(DWORD scanCode){
 return scanCode==DIK_W||scanCode==DIK_A||scanCode==DIK_S||scanCode==DIK_D||
        scanCode==DIK_K||scanCode==DIK_L;
}
static void mergeMenuKeys(BYTE* keys){
 if(!controllerMenuActive())return;
 if(!InterlockedCompareExchange(&secondMenuKeyboardPassthrough,0,0))
  keys[DIK_RETURN]=keys[DIK_ESCAPE]=0;
 if(filterSplitKeyboardKeys())for(DWORD key:{DIK_W,DIK_A,DIK_S,DIK_D,DIK_K,DIK_L})keys[key]=0;
 if(menuKeys[0])keys[DIK_RETURN]=0x80;
 if(menuKeys[1])keys[DIK_ESCAPE]=0x80;
}
static void filterMenuEvents(DIDEVICEOBJECTDATA* events,DWORD* count){
 if(!events||!controllerMenuActive())return;
 DWORD out=0;
 for(DWORD i=0;i<*count;i++){
  const DWORD key=events[i].dwOfs;
  if(!InterlockedCompareExchange(&secondMenuKeyboardPassthrough,0,0)&&
     (key==DIK_RETURN||key==DIK_ESCAPE))continue;
  if(filterSplitKeyboardKeys()&&isSplitKeyboardKey(key))continue;
  events[out++]=events[i];
 }
 *count=out;
}
static void appendMenuEvents(DIDEVICEOBJECTDATA* events,DWORD* count,DWORD capacity,DWORD flags,ULONGLONG& cursor){
 for(const auto& transition:menuTransitions)if(transition.serial>cursor&&*count<capacity){
  DIDEVICEOBJECTDATA e={};e.dwOfs=transition.key?DIK_ESCAPE:DIK_RETURN;e.dwData=transition.down?0x80:0;e.dwTimeStamp=transition.time;
  if(events)events[*count]=e;++*count;if(!(flags&DIGDD_PEEK))cursor=transition.serial;
 }
}

#include "rumble.inc"

static DWORD WINAPI controllerWorker(void*){
 for(;;){
#ifndef PADFIX_PLATFORM_ONLY
  sample();pollControllerMenu();
#endif
  updateRumble();Sleep(8);
 }
}
static DWORD WINAPI nativeSetState(DWORD slot,XINPUT_VIBRATION* vibration){
 initializeAll();if(!gameSetXState)return ERROR_DEVICE_NOT_CONNECTED;
 if(!vibration)return ERROR_BAD_ARGUMENTS;
 XINPUT_VIBRATION value=foreground()?*vibration:XINPUT_VIBRATION{};
 std::lock_guard<std::recursive_mutex> lock(mutex);DWORD result=gameSetXState(slot,&value);
 if(result==ERROR_SUCCESS&&slot<4)nativeVibration[slot]=value;return result;
}
static void startControllerWorker(){
#ifndef PADFIX_TEST
 static std::once_flag once;std::call_once(once,[]{
  if(!enabled&&_stricmp(gameExeName,"Launch.exe"))return;
  HMODULE pinned=nullptr;GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,(LPCSTR)controllerWorker,&pinned);
  HANDLE h=CreateThread(nullptr,0,controllerWorker,nullptr,0,nullptr);if(h)CloseHandle(h);
 });
#endif
}
