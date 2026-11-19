// Application-local menu ownership and DirectInput -> XInput force feedback.
static const ULONG_PTR controllerKeyTag=0x4D55534F;
static bool menuKeys[2]={},menuButtons[2]={};
static ULONGLONG menuKeyUntil[2]={};
struct MenuTransition{ULONGLONG serial;unsigned key;bool down;DWORD time;};
static ULONGLONG menuSerial=0;
static std::deque<MenuTransition> menuTransitions;
static bool controllerMenuActive(){std::lock_guard<std::recursive_mutex> lock(mutex);return controllerTakeover&&menuSceneActive!=0&&armed&&wasConnected&&foreground();}
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
 const bool active=controllerMenuActive()&&foreground()&&armed;
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
static void mergeMenuKeys(BYTE* keys){
 if(!controllerMenuActive())return;
 keys[DIK_RETURN]=keys[DIK_ESCAPE]=0;
 if(menuKeys[0])keys[DIK_RETURN]=0x80;
 if(menuKeys[1])keys[DIK_ESCAPE]=0x80;
}
static void filterMenuEvents(DIDEVICEOBJECTDATA* events,DWORD* count){
 if(!events||!controllerMenuActive())return;
 DWORD out=0;for(DWORD i=0;i<*count;i++)if(events[i].dwOfs!=DIK_RETURN&&events[i].dwOfs!=DIK_ESCAPE)events[out++]=events[i];*count=out;
}
static void appendMenuEvents(DIDEVICEOBJECTDATA* events,DWORD* count,DWORD capacity,DWORD flags,ULONGLONG& cursor){
 for(const auto& transition:menuTransitions)if(transition.serial>cursor&&*count<capacity){
  DIDEVICEOBJECTDATA e={};e.dwOfs=transition.key?DIK_ESCAPE:DIK_RETURN;e.dwData=transition.down?0x80:0;e.dwTimeStamp=transition.time;
  if(events)events[*count]=e;++*count;if(!(flags&DIGDD_PEEK))cursor=transition.serial;
 }
}

class PadEffect;
static std::vector<PadEffect*> padEffects;
static DWORD rumbleGain[2]={10000,10000};
static bool rumblePaused[2]={},rumbleEnabled[2]={true,true};
static ULONGLONG rumblePauseAt[2]={};
static XINPUT_VIBRATION lastVibration[2]={};
static XINPUT_VIBRATION nativeVibration[4]={};
static void writeRumble(unsigned player,WORD low,WORD high){
 int slot=player?secondSlot:selectedSlot;if(!player&&selectedBackend==2)return;
 if(slot<0||!setXState)return;
 auto& old=lastVibration[player];if(old.wLeftMotorSpeed==low&&old.wRightMotorSpeed==high)return;
 XINPUT_VIBRATION v={low,high};if(setXState((DWORD)slot,&v)==ERROR_SUCCESS)old=v;
}
class PadEffect final:public IDirectInputEffect {
 LONG refs=1;
public:
 GUID kind;unsigned player;DWORD duration=0,gain=10000,delay=0,iterations=1;
 LONG magnitude=0;bool running=false,downloaded=false;ULONGLONG started=0;
 explicit PadEffect(REFGUID g,unsigned p):kind(g),player(p){padEffects.push_back(this);}
 ~PadEffect(){padEffects.erase(std::remove(padEffects.begin(),padEffects.end(),this),padEffects.end());updateRumble();}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p)override{if(!p)return E_POINTER;*p=nullptr;if(id!=IID_IUnknown&&id!=IID_IDirectInputEffect)return E_NOINTERFACE;*p=this;AddRef();return S_OK;}
 ULONG STDMETHODCALLTYPE AddRef()override{return InterlockedIncrement(&refs);}
 ULONG STDMETHODCALLTYPE Release()override{std::lock_guard<std::recursive_mutex> lock(mutex);ULONG n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
 HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE,DWORD,REFGUID)override{return DI_OK;}
 HRESULT STDMETHODCALLTYPE GetEffectGuid(LPGUID p)override{if(!p)return E_POINTER;*p=kind;return DI_OK;}
 HRESULT STDMETHODCALLTYPE GetParameters(LPDIEFFECT e,DWORD flags)override{
  if(!e||e->dwSize<sizeof(DIEFFECT))return DIERR_INVALIDPARAM;
  if(flags&DIEP_DURATION)e->dwDuration=duration;if(flags&DIEP_GAIN)e->dwGain=gain;if(flags&DIEP_STARTDELAY)e->dwStartDelay=delay;
  if(flags&DIEP_TYPESPECIFICPARAMS){
   DWORD need=kind==GUID_ConstantForce?sizeof(DICONSTANTFORCE):kind==GUID_RampForce?sizeof(DIRAMPFORCE):sizeof(DIPERIODIC);
   if(e->cbTypeSpecificParams<need||!e->lpvTypeSpecificParams){e->cbTypeSpecificParams=need;return DIERR_MOREDATA;}
   memset(e->lpvTypeSpecificParams,0,need);e->cbTypeSpecificParams=need;
   if(kind==GUID_ConstantForce)((DICONSTANTFORCE*)e->lpvTypeSpecificParams)->lMagnitude=magnitude;
   else if(kind==GUID_RampForce){auto r=(DIRAMPFORCE*)e->lpvTypeSpecificParams;r->lStart=r->lEnd=magnitude;}
   else ((DIPERIODIC*)e->lpvTypeSpecificParams)->dwMagnitude=(DWORD)magnitude;
  }
  return DI_OK;
 }
 HRESULT STDMETHODCALLTYPE SetParameters(LPCDIEFFECT e,DWORD flags)override{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if(!e||e->dwSize<sizeof(DIEFFECT))return DIERR_INVALIDPARAM;
  if(flags&DIEP_DURATION)duration=e->dwDuration;if(flags&DIEP_GAIN)gain=std::min(e->dwGain,10000UL);if(flags&DIEP_STARTDELAY)delay=e->dwStartDelay;
  if(flags&DIEP_TYPESPECIFICPARAMS){
   DWORD need=kind==GUID_ConstantForce?sizeof(DICONSTANTFORCE):kind==GUID_RampForce?sizeof(DIRAMPFORCE):sizeof(DIPERIODIC);
   if(!e->lpvTypeSpecificParams||e->cbTypeSpecificParams<need)return DIERR_INVALIDPARAM;
   if(kind==GUID_ConstantForce)magnitude=((DICONSTANTFORCE*)e->lpvTypeSpecificParams)->lMagnitude;
   else if(kind==GUID_RampForce){auto r=(DIRAMPFORCE*)e->lpvTypeSpecificParams;magnitude=std::max(abs(r->lStart),abs(r->lEnd));}
   else magnitude=(LONG)std::min(((DIPERIODIC*)e->lpvTypeSpecificParams)->dwMagnitude,10000UL);
   magnitude=std::clamp(magnitude,-10000L,10000L);
  }
  if(!(flags&DIEP_NODOWNLOAD))downloaded=true;
  if(flags&DIEP_START)return Start(1,0);updateRumble();return DI_OK;
 }
 HRESULT STDMETHODCALLTYPE Start(DWORD n,DWORD flags)override{
  std::lock_guard<std::recursive_mutex> lock(mutex);if(!n)return DIERR_INVALIDPARAM;
  if(flags&DIES_SOLO)for(auto e:padEffects)if(e->player==player)e->running=false;
  if((flags&DIES_NODOWNLOAD)&&!downloaded)return DIERR_NOTDOWNLOADED;
  downloaded=true;iterations=n;started=GetTickCount64();running=true;updateRumble();return DI_OK;
 }
 HRESULT STDMETHODCALLTYPE Stop()override{std::lock_guard<std::recursive_mutex> lock(mutex);running=false;updateRumble();return DI_OK;}
 HRESULT STDMETHODCALLTYPE GetEffectStatus(LPDWORD p)override{if(!p)return E_POINTER;std::lock_guard<std::recursive_mutex> lock(mutex);updateRumble();*p=running?DIEGES_PLAYING:0;return DI_OK;}
 HRESULT STDMETHODCALLTYPE Download()override{downloaded=true;return DI_OK;}
 HRESULT STDMETHODCALLTYPE Unload()override{Stop();downloaded=false;return DI_OK;}
 HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE)override{return DIERR_UNSUPPORTED;}
};
static bool supportsEffect(REFGUID g){return g==GUID_ConstantForce||g==GUID_RampForce||g==GUID_Sine||g==GUID_Square||g==GUID_Triangle||g==GUID_SawtoothUp||g==GUID_SawtoothDown;}
static DIEFFECTINFOA effectInfo(REFGUID g){
 DIEFFECTINFOA i={};i.dwSize=sizeof(i);i.guid=g;i.dwEffType=g==GUID_ConstantForce?DIEFT_CONSTANTFORCE:g==GUID_RampForce?DIEFT_RAMPFORCE:DIEFT_PERIODIC;
 i.dwStaticParams=i.dwDynamicParams=DIEP_DURATION|DIEP_GAIN|DIEP_STARTDELAY|DIEP_TYPESPECIFICPARAMS;
 strcpy_s(i.tszName,"XInput motor vibration");return i;
}
static void updateRumble(){
 std::lock_guard<std::recursive_mutex> lock(mutex);ULONGLONG now=GetTickCount64();DWORD strength[2]={};
 if(!foreground()&&gameSetXState)for(DWORD slot=0;slot<4;slot++){
  auto& v=nativeVibration[slot];if(v.wLeftMotorSpeed||v.wRightMotorSpeed){XINPUT_VIBRATION zero={};if(gameSetXState(slot,&zero)==ERROR_SUCCESS)v=zero;}
 }
 for(auto e:padEffects)if(e->running){
  if(rumblePaused[e->player])continue;
  ULONGLONG elapsed=(now-e->started)*1000;
  if(elapsed<e->delay)continue;
  if(e->duration!=INFINITE&&e->iterations!=INFINITE&&elapsed-e->delay>=(ULONGLONG)e->duration*e->iterations){e->running=false;continue;}
  XINPUT_STATE state={};int slot=e->player?secondSlot:selectedSlot;
  if(!foreground()||slot<0||!getXState||getXState((DWORD)slot,&state)!=ERROR_SUCCESS){e->running=false;continue;}
  DWORD value=(DWORD)((ULONGLONG)abs(e->magnitude)*e->gain/10000);
  strength[e->player]=std::max(strength[e->player],value);
 }
 for(unsigned p=0;p<2;p++){
  DWORD v=rumbleEnabled[p]&&!rumblePaused[p]?strength[p]*rumbleGain[p]/10000:0;
  WORD motor=(WORD)((ULONGLONG)v*65535/10000);writeRumble(p,motor,motor);
 }
}
static void stopRumble(unsigned player){
 std::lock_guard<std::recursive_mutex> lock(mutex);
 for(auto e:padEffects)if(e->player==player)e->running=false;
 writeRumble(player,0,0);
}
static DWORD WINAPI controllerWorker(void*){
 for(;;){sample();pollControllerMenu();updateRumble();Sleep(8);}
}
static DWORD WINAPI nativeGetState(DWORD slot,XINPUT_STATE* state){
 initializeAll();startControllerWorker();
 if(!gameGetXState)return ERROR_DEVICE_NOT_CONNECTED;
 DWORD result=gameGetXState(slot,state);
 if(result==ERROR_SUCCESS&&state){std::lock_guard<std::recursive_mutex> lock(mutex);
  if(!foreground())state->Gamepad={};
  else if(controllerTakeover&&menuSceneActive){
   if((int)slot!=selectedSlot)state->Gamepad={};
   else state->Gamepad.wButtons&=~(XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_B);
  }
 }
 return result;
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
