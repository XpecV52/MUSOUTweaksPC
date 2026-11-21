#include <hidsdi.h>
#include <memory>

static bool isDualSense(DWORD product){
 return product==0x0ce6054c||product==0x0df2054c;
}

static bool isSonyRumblePad(DWORD product){
 return product==0x05c4054c||product==0x09cc054c||isDualSense(product);
}

static DWORD sonyOutputCrc(const BYTE* report,DWORD size){
 DWORD crc=0xffffffff;
 auto append=[&](BYTE value){
  crc^=value;
  for(int bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320UL&(0UL-(crc&1)));
 };
 append(0xa2);
 for(DWORD i=0;i<size;++i)append(report[i]);
 return ~crc;
}

static void ds4MotorReport(BYTE* report,DWORD size,WORD low,WORD high){
 memset(report,0,size);
 if(size==78){
  report[0]=0x11;
  report[1]=0xc0;
  report[3]=0x01;
  report[6]=(BYTE)(high>>8);
  report[7]=(BYTE)(low>>8);
  const DWORD crc=sonyOutputCrc(report,size-4);
  memcpy(report+size-4,&crc,sizeof(crc));
 }else{
  report[0]=0x05;
  report[1]=0x01;
  report[4]=(BYTE)(high>>8);
  report[5]=(BYTE)(low>>8);
 }
}

static void dualSenseMotorReport(BYTE* report,DWORD size,BYTE sequence,bool improved,WORD low,WORD high){
 memset(report,0,size);
 const bool bluetooth=size==78;
 report[0]=bluetooth?0x31:0x02;
 BYTE* payload=report+1;
 if(bluetooth){
  report[1]=(BYTE)(sequence<<4);
  report[2]=0x10;
  payload=report+3;
 }
 // Compatible vibration and audio haptics are mutually exclusive.
 // Updated firmware enables improved vibration in the third flag byte.
 const bool active=low||high;
 if(active){
  payload[0]=improved?0x02:0x03;
  if(improved)payload[38]=0x04;
  payload[2]=(BYTE)(high>>8);
  payload[3]=(BYTE)(low>>8);
 }
 if(bluetooth){
  const DWORD crc=sonyOutputCrc(report,size-4);
  memcpy(report+size-4,&crc,sizeof(crc));
 }
}

static bool getHidPath(void* device,DIPROPGUIDANDPATH& path){
 // GetProperty has the same signature for ANSI and Unicode devices.
 using GetPropertyFn=HRESULT (WINAPI*)(void*,REFGUID,LPDIPROPHEADER);
 auto getProperty=(GetPropertyFn)(*static_cast<void***>(device))[5];
 path={};
 path.diph={sizeof(path),sizeof(path.diph),0,DIPH_DEVICE};
 return SUCCEEDED(getProperty(device,DIPROP_GUIDANDPATH,&path.diph))&&path.wszPath[0];
}

static DWORD nativePadProduct(void* device,bool wide){
 if(wide){
  DIDEVICEINSTANCEW info={sizeof(info)};
  return SUCCEEDED(static_cast<IDirectInputDevice8W*>(device)->GetDeviceInfo(&info))?info.guidProduct.Data1:0;
 }
 DIDEVICEINSTANCEA info={sizeof(info)};
 return SUCCEEDED(static_cast<IDirectInputDevice8A*>(device)->GetDeviceInfo(&info))?info.guidProduct.Data1:0;
}

class SonyRumble{
 HANDLE device=INVALID_HANDLE_VALUE;
 OVERLAPPED writeOperation={};
 bool pending=false,motorsKnown=true,logged=false,loggedError=false;
 bool dualSense=false,improved=false;
 DWORD size=0;
 BYTE sequence=0;
 BYTE report[78]={};
 XINPUT_VIBRATION motors={};
public:
 SonyRumble()=default;
 SonyRumble(const SonyRumble&)=delete;
 SonyRumble& operator=(const SonyRumble&)=delete;
 ~SonyRumble(){close();}
 bool isOpen()const{return device!=INVALID_HANDLE_VALUE;}
 void clear(){
  if(pending)WaitForSingleObject(writeOperation.hEvent,32);
  send(0,0);
  if(pending)WaitForSingleObject(writeOperation.hEvent,32);
 }
 void close(){
  if(isOpen()){
   if(pending){
    CancelIoEx(device,&writeOperation);
    DWORD transferred=0;
    GetOverlappedResult(device,&writeOperation,&transferred,TRUE);
   }
   CloseHandle(device);
   CloseHandle(writeOperation.hEvent);
   device=INVALID_HANDLE_VALUE;
  }
  writeOperation={};
  pending=false;
  motorsKnown=true;
  motors={};
  logged=false;
  loggedError=false;
  size=0;
  sequence=0;
 }
 bool open(const wchar_t* path,DWORD product){
  if(isOpen())return true;
  dualSense=isDualSense(product);
  device=CreateFileW(path,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
  if(!isOpen())device=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
  if(!isOpen())return false;
  static HMODULE hid=systemDll("hid.dll");
  static auto getData=hid?(decltype(&HidD_GetPreparsedData))GetProcAddress(hid,"HidD_GetPreparsedData"):nullptr;
  static auto getCaps=hid?(decltype(&HidP_GetCaps))GetProcAddress(hid,"HidP_GetCaps"):nullptr;
  static auto freeData=hid?(decltype(&HidD_FreePreparsedData))GetProcAddress(hid,"HidD_FreePreparsedData"):nullptr;
  static auto getFeature=hid?(decltype(&HidD_GetFeature))GetProcAddress(hid,"HidD_GetFeature"):nullptr;
  PHIDP_PREPARSED_DATA data=nullptr;
  HIDP_CAPS caps={};
  if(getData&&getCaps&&freeData&&getData(device,&data)){
   if(getCaps(data,&caps)!=HIDP_STATUS_SUCCESS)caps={};
   freeData(data);
  }
  // Windows reports the largest declared report (547 bytes on a Bluetooth
  // DS4 v1), including reports other than the 78-byte motor report.
  if(caps.OutputReportByteLength>=78)size=78;
  else if(dualSense&&caps.OutputReportByteLength>=48)size=48;
  else if(!dualSense&&caps.OutputReportByteLength==32)size=32;
  writeOperation.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  if(!size||!writeOperation.hEvent){close();return false;}
  if(dualSense){
   unsigned firmware=0;
   if(getFeature&&caps.FeatureReportByteLength>=46){
    std::vector<BYTE> data(caps.FeatureReportByteLength);
    data[0]=0x20;
    if(getFeature(device,data.data(),(ULONG)data.size()))firmware=data[44]|(data[45]<<8);
   }
   improved=product==0x0df2054c||firmware>=0x0224||(!firmware&&size==78);
  }
  logf("Sony force feedback ready VIDPID=%08lx transport=%s",product,size==78?"Bluetooth":"USB");
  return true;
 }
 void send(WORD low,WORD high){
  if(!isOpen())return;
  DWORD transferred=0;
  if(pending){
   if(!GetOverlappedResult(device,&writeOperation,&transferred,FALSE)){
    const DWORD error=GetLastError();
    if(error==ERROR_IO_INCOMPLETE)return;
    if(!loggedError){logf("Sony force feedback write completion failed error=%lu",error);loggedError=true;}
    motorsKnown=false;
   }else if(!logged&&(motors.wLeftMotorSpeed||motors.wRightMotorSpeed)){
    logf("Sony force feedback sent");
    logged=true;
   }
   pending=false;
  }
  if(motorsKnown&&motors.wLeftMotorSpeed==low&&motors.wRightMotorSpeed==high)return;
  if(dualSense){
   dualSenseMotorReport(report,size,sequence,improved,low,high);
   sequence=(sequence+1)&15;
  }else ds4MotorReport(report,size,low,high);
  ResetEvent(writeOperation.hEvent);
  if(!WriteFile(device,report,size,&transferred,&writeOperation)){
   const DWORD error=GetLastError();
   if(error!=ERROR_IO_PENDING){
    if(!loggedError){logf("Sony force feedback WriteFile failed error=%lu",error);loggedError=true;}
    return;
   }
   pending=true;
  }
  if(!pending&&!logged&&(low||high)){logf("Sony force feedback sent");logged=true;}
  motors={low,high};
  motorsKnown=true;
 }
};

struct XInputRumble {
 int slot=-1;
 XINPUT_VIBRATION motors={};
 bool logged=false;
 void send(int next,WORD low,WORD high){
  if(!setXState)return;
  if(next!=slot){
   if(slot>=0&&(motors.wLeftMotorSpeed||motors.wRightMotorSpeed)){
    XINPUT_VIBRATION zero={};
    setXState((DWORD)slot,&zero);
   }
   slot=next;
   motors={};
   logged=false;
  }
  if(slot<0||(motors.wLeftMotorSpeed==low&&motors.wRightMotorSpeed==high))return;
  XINPUT_VIBRATION vibration={low,high};
  const DWORD result=setXState((DWORD)slot,&vibration);
  if(!logged&&(low||high)){logf("XInput force feedback slot=%d low=%u high=%u result=%lu",slot,low,high,result);logged=true;}
  if(result==ERROR_SUCCESS)motors=vibration;
  if(result==ERROR_DEVICE_NOT_CONNECTED)motors={};
 }
};

struct NativePadRumble{
 SonyRumble hid;
 XInputRumble xinput;
 int slot=-1;
 DWORD gain=10000,strength=0;
 ULONGLONG pauseAt=0;
 bool connected=true,paused=false,enabled=true;
 void send(WORD value){if(hid.isOpen())hid.send(value,value);else xinput.send(slot,value,value);}
};

class PadEffect;
static std::vector<PadEffect*> padEffects;
static DWORD rumbleGain[2]={10000,10000};
static bool rumblePaused[2]={},rumbleEnabled[2]={true,true};
static ULONGLONG rumblePauseAt[2]={};
static XInputRumble virtualRumble[2];
static SonyRumble ds4Rumble;
static bool ds4RumbleTried=false;
static XINPUT_VIBRATION nativeVibration[4]={};
static std::unordered_map<void*,std::shared_ptr<NativePadRumble>> nativePadRumble;

static void disconnectDs4Rumble(){
 ds4Rumble.clear();
 ds4Rumble.close();
 ds4RumbleTried=false;
}

static void writeRumble(unsigned player,WORD low,WORD high){
 const bool ds4=!player&&selectedBackend==2;
 virtualRumble[player].send(ds4?-1:player?secondSlot:selectedSlot,low,high);
 if(!ds4)return;
 if(sourcePad&&!ds4RumbleTried&&(low||high)){
  ds4RumbleTried=true;
  DIPROPGUIDANDPATH path={};
  const DWORD product=nativePadProduct(sourcePad,false);
  if(!getHidPath(sourcePad,path)||!ds4Rumble.open(path.wszPath,product))logf("Sony force feedback output could not be opened");
 }
 ds4Rumble.send(low,high);
}
class PadEffect final:public IDirectInputEffect {
 LONG refs=1;
 bool loggedStart=false;
public:
 GUID kind;unsigned player;DWORD duration=0,gain=10000,delay=0,iterations=1;
 std::shared_ptr<NativePadRumble> native;
 LONG magnitude=0;bool running=false,downloaded=false;ULONGLONG started=0;
 explicit PadEffect(REFGUID g,unsigned p,std::shared_ptr<NativePadRumble> target={})
  :kind(g),player(p),native(std::move(target)){padEffects.push_back(this);}
 ~PadEffect(){padEffects.erase(std::remove(padEffects.begin(),padEffects.end(),this),padEffects.end());updateRumble();}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p)override{if(!p)return E_POINTER;*p=nullptr;if(id!=IID_IUnknown&&id!=IID_IDirectInputEffect)return E_NOINTERFACE;*p=this;AddRef();return S_OK;}
 ULONG STDMETHODCALLTYPE AddRef()override{return InterlockedIncrement(&refs);}
 ULONG STDMETHODCALLTYPE Release()override{std::lock_guard<std::recursive_mutex> lock(mutex);ULONG n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
 HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE,DWORD,REFGUID)override{return DI_OK;}
 HRESULT STDMETHODCALLTYPE GetEffectGuid(LPGUID p)override{if(!p)return E_POINTER;*p=kind;return DI_OK;}
 HRESULT STDMETHODCALLTYPE GetParameters(LPDIEFFECT e,DWORD flags)override{
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if(!e||e->dwSize<offsetof(DIEFFECT,dwStartDelay))return DIERR_INVALIDPARAM;
  if(flags&DIEP_DURATION)e->dwDuration=duration;if(flags&DIEP_GAIN)e->dwGain=gain;
  if((flags&DIEP_STARTDELAY)&&e->dwSize>=sizeof(DIEFFECT))e->dwStartDelay=delay;
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
  if(!e||e->dwSize<offsetof(DIEFFECT,dwStartDelay))return DIERR_INVALIDPARAM;
  if(flags&DIEP_DURATION)duration=e->dwDuration;if(flags&DIEP_GAIN)gain=std::min(e->dwGain,10000UL);
  if((flags&DIEP_STARTDELAY)&&e->dwSize>=sizeof(DIEFFECT))delay=e->dwStartDelay;
  if(flags&DIEP_TYPESPECIFICPARAMS){
   DWORD need=kind==GUID_ConstantForce?sizeof(DICONSTANTFORCE):kind==GUID_RampForce?sizeof(DIRAMPFORCE):sizeof(DIPERIODIC);
   if(!e->lpvTypeSpecificParams||e->cbTypeSpecificParams<need)return DIERR_INVALIDPARAM;
   if(kind==GUID_ConstantForce)magnitude=((DICONSTANTFORCE*)e->lpvTypeSpecificParams)->lMagnitude;
   else if(kind==GUID_RampForce){auto r=(const DIRAMPFORCE*)e->lpvTypeSpecificParams;magnitude=std::max(abs(std::clamp(r->lStart,-10000L,10000L)),abs(std::clamp(r->lEnd,-10000L,10000L)));}
   else magnitude=(LONG)std::min(((DIPERIODIC*)e->lpvTypeSpecificParams)->dwMagnitude,10000UL);
   magnitude=std::clamp(magnitude,-10000L,10000L);
  }
  if(!(flags&DIEP_NODOWNLOAD))downloaded=true;
  if(flags&DIEP_START)return Start(1,0);updateRumble();return DI_OK;
 }
 HRESULT STDMETHODCALLTYPE Start(DWORD n,DWORD flags)override{
  std::lock_guard<std::recursive_mutex> lock(mutex);if(!n)return DIERR_INVALIDPARAM;
  if(flags&DIES_SOLO)for(auto e:padEffects)if(e->player==player&&e->native==native)e->running=false;
  if((flags&DIES_NODOWNLOAD)&&!downloaded)return DIERR_NOTDOWNLOADED;
  downloaded=true;iterations=n;
  // Several Koei builds submit a hit effect with dwDuration=0 and expect the
  // driver to turn it into a short impulse. Treating zero as an immediate
  // expiry made those impacts completely silent on virtual controllers.
  if(!duration&&magnitude)duration=80000;
  // Some Koei ports leave the actuator flag disabled after device startup
  // until the options page is toggled once. Starting a real effect is an
  // explicit request, so restore the enabled state here.
  rumbleEnabled[player]=true;
  started=GetTickCount64();running=true;
  if(!loggedStart){logf("DirectInput effect started native=%d player=%u magnitude=%ld duration=%lu gain=%lu kind=%08lx",!!native,player+1,magnitude,duration,gain,kind.Data1);loggedStart=true;}
  updateRumble();return DI_OK;
 }
 HRESULT STDMETHODCALLTYPE Stop()override{std::lock_guard<std::recursive_mutex> lock(mutex);running=false;updateRumble();return DI_OK;}
 HRESULT STDMETHODCALLTYPE GetEffectStatus(LPDWORD p)override{if(!p)return E_POINTER;std::lock_guard<std::recursive_mutex> lock(mutex);updateRumble();*p=running?DIEGES_PLAYING:0;return DI_OK;}
 HRESULT STDMETHODCALLTYPE Download()override{std::lock_guard<std::recursive_mutex> lock(mutex);downloaded=true;return DI_OK;}
 HRESULT STDMETHODCALLTYPE Unload()override{std::lock_guard<std::recursive_mutex> lock(mutex);downloaded=false;return Stop();}
 HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE)override{return DIERR_UNSUPPORTED;}
};
static bool supportsEffect(REFGUID g){return g==GUID_ConstantForce||g==GUID_RampForce||g==GUID_Sine||g==GUID_Square||g==GUID_Triangle||g==GUID_SawtoothUp||g==GUID_SawtoothDown;}
static DIEFFECTINFOA effectInfo(REFGUID g){
 DIEFFECTINFOA i={};i.dwSize=sizeof(i);i.guid=g;i.dwEffType=g==GUID_ConstantForce?DIEFT_CONSTANTFORCE:g==GUID_RampForce?DIEFT_RAMPFORCE:DIEFT_PERIODIC;
 i.dwStaticParams=i.dwDynamicParams=DIEP_DURATION|DIEP_GAIN|DIEP_STARTDELAY|DIEP_TYPESPECIFICPARAMS;
 strcpy_s(i.tszName,"Controller motor vibration");return i;
}
static bool xinputConnected(int slot){
 XINPUT_STATE state={};
 return slot>=0&&getXState&&getXState((DWORD)slot,&state)==ERROR_SUCCESS;
}
#if defined(PADFIX_PLATFORM_ONLY) || defined(PADFIX_TEST)
static BYTE* ssm7RumbleBase;
static void (WINAPI *originalSsm7PadUpdate)(BYTE*);
static XInputRumble ssm7SteamRumble;
static bool ssm7FallbackEnabled=false;
static ULONGLONG ssm7RumbleDeadline[2]={};
static WORD ssm7RumbleAmplitude[2]={};
static ULONGLONG ssm7PreviousQueue[2]={};

static const BYTE* ssm7FallbackPad(){
 if(!ssm7RumbleBase||gameSetXState)return nullptr;
 if(*(const DWORD*)(ssm7RumbleBase+0x6afe34)){
  const BYTE* const* devices=*(const BYTE* const* const*)(ssm7RumbleBase+0x6afe2c);
  // The first native device only owns this output after its effect was created.
  if(devices&&devices[1]&&devices[1][0x1b0])return nullptr;
 }
 const BYTE* settings=*(const BYTE* const*)(ssm7RumbleBase+0x6aef68);
 if(!settings||(signed char)settings[0x40]>0)return nullptr;
 return *(const BYTE* const*)(ssm7RumbleBase+0x6af1c4);
}
static void serviceSsm7RumbleFallback(){
 if(!ssm7RumbleBase)return;
 if(!ssm7RumbleDeadline[0]&&!ssm7RumbleDeadline[1]&&
    !ssm7SteamRumble.motors.wLeftMotorSpeed&&!ssm7SteamRumble.motors.wRightMotorSpeed)return;
 const int slot=slotConfig>=0?slotConfig:0;
 if(!xinputConnected(slot)){
  ssm7RumbleDeadline[0]=ssm7RumbleDeadline[1]=0;
  ssm7SteamRumble.send(-1,0,0);
  return;
 }
 if(!foreground()||!ssm7FallbackEnabled)ssm7RumbleDeadline[0]=ssm7RumbleDeadline[1]=0;
 const auto now=GetTickCount64();
 ssm7SteamRumble.send(slot,now<ssm7RumbleDeadline[1]?ssm7RumbleAmplitude[1]:0,
                           now<ssm7RumbleDeadline[0]?ssm7RumbleAmplitude[0]:0);
 for(auto& deadline:ssm7RumbleDeadline)if(now>=deadline)deadline=0;
}
static void WINAPI readSsm7RumbleQueue(BYTE* pad){
 {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  ssm7FallbackEnabled=pad&&pad==ssm7FallbackPad();
  if(ssm7FallbackEnabled&&foreground()&&xinputConnected(slotConfig>=0?slotConfig:0)){
   // The stock update clears both queues when there is no DirectInput pad.
   const auto now=GetTickCount64();
   const DWORD* duration=(const DWORD*)(pad+0xb8);
   for(unsigned motor=0;motor<2;++motor){
    const ULONGLONG request=((ULONGLONG)duration[motor]<<8)|pad[0xc0+motor];
    if(!duration[motor]||!pad[0xc0+motor]||request==ssm7PreviousQueue[motor])continue;
    ssm7RumbleDeadline[motor]=now+((ULONGLONG)std::min(duration[motor],120UL)*1000+59)/60;
    ssm7RumbleAmplitude[motor]=motor?WORD(pad[0xc1]*257):65535;
   }
  }
 }
 originalSsm7PadUpdate(pad);
 std::lock_guard<std::recursive_mutex> lock(mutex);
 if(ssm7FallbackEnabled&&pad==ssm7FallbackPad()){
  const DWORD* duration=(const DWORD*)(pad+0xb8);
  for(unsigned motor=0;motor<2;++motor)ssm7PreviousQueue[motor]=((ULONGLONG)duration[motor]<<8)|pad[0xc0+motor];
 }else{
  ssm7PreviousQueue[0]=ssm7PreviousQueue[1]=0;
 }
 serviceSsm7RumbleFallback();
}
static void installSsm7RumbleFallback(){
 if(_stricmp(gameExeName,"Launch.exe"))return;
 std::lock_guard<std::recursive_mutex> lock(mutex);
 if(ssm7RumbleBase)return;
 BYTE* base=(BYTE*)GetModuleHandleA(nullptr);
 if(!base)return;
 const auto* dos=(const IMAGE_DOS_HEADER*)base;
 const auto* nt=(const IMAGE_NT_HEADERS*)(base+dos->e_lfanew);
 constexpr DWORD update=0x1cb320,cleanup=0x1cb6c7;
 constexpr BYTE prefix[]={0x81,0xec,0xf8,0,0,0};
 constexpr BYTE tail[]={0xc2,0x04,0};
 if(nt->OptionalHeader.SizeOfImage<0x6afe38||memcmp(base+cleanup,tail,sizeof(tail)))return;
 if(buildInlineTrampoline(base+update,prefix,sizeof(prefix),(void*)readSsm7RumbleQueue,
                          (void**)&originalSsm7PadUpdate)){
  ssm7RumbleBase=base;
  logf("SSM7 XInput force feedback fallback installed");
 }
}
#endif
static void updateRumble(){
 std::lock_guard<std::recursive_mutex> lock(mutex);
 const ULONGLONG now=GetTickCount64();
 const bool hasFocus=foreground();
 DWORD strength[2]={};
 if(!hasFocus&&gameSetXState)for(DWORD slot=0;slot<4;slot++){
  auto& v=nativeVibration[slot];if(v.wLeftMotorSpeed||v.wRightMotorSpeed){XINPUT_VIBRATION zero={};if(gameSetXState(slot,&zero)==ERROR_SUCCESS)v=zero;}
 }
 for(auto& entry:nativePadRumble)entry.second->strength=0;
 for(auto e:padEffects)if(e->running){
  auto* target=e->native.get();
  if(target?target->paused:rumblePaused[e->player])continue;
  ULONGLONG elapsed=(now-e->started)*1000;
  if(elapsed<e->delay)continue;
  if(e->duration!=INFINITE&&e->iterations!=INFINITE&&elapsed-e->delay>=(ULONGLONG)e->duration*e->iterations){e->running=false;continue;}
  const bool connected=target
   ?target->connected&&(target->hid.isOpen()||xinputConnected(target->slot))
   :(!e->player&&selectedBackend==2?sourcePad!=nullptr:xinputConnected(e->player?secondSlot:selectedSlot));
  if(!hasFocus||!connected){e->running=false;continue;}
  DWORD value=(DWORD)((ULONGLONG)abs(e->magnitude)*e->gain/10000);
  DWORD& output=target?target->strength:strength[e->player];
  output=std::max(output,value);
 }
 for(unsigned p=0;p<2;p++){
  DWORD v=rumbleEnabled[p]&&!rumblePaused[p]?strength[p]*rumbleGain[p]/10000:0;
  v=std::min(v*rumbleScale/100,10000UL);
  WORD motor=(WORD)((ULONGLONG)v*65535/10000);writeRumble(p,motor,motor);
 }
 for(auto& entry:nativePadRumble){
  auto& target=*entry.second;
  const DWORD value=target.enabled&&!target.paused?target.strength*target.gain/10000:0;
  target.send((WORD)((ULONGLONG)value*65535/10000));
 }
#if defined(PADFIX_PLATFORM_ONLY) || defined(PADFIX_TEST)
 serviceSsm7RumbleFallback();
#endif
}
static void stopRumble(unsigned player){
 std::lock_guard<std::recursive_mutex> lock(mutex);
 for(auto e:padEffects)if(!e->native&&e->player==player)e->running=false;
 writeRumble(player,0,0);
}

static HRESULT createPadEffect(REFGUID kind,LPCDIEFFECT params,LPDIRECTINPUTEFFECT* out,
 LPUNKNOWN outer,unsigned player,std::shared_ptr<NativePadRumble> target={}){
 if(!out)return E_POINTER;
 *out=nullptr;
 if(outer)return CLASS_E_NOAGGREGATION;
 if(!supportsEffect(kind))return DIERR_UNSUPPORTED;
 std::lock_guard<std::recursive_mutex> lock(mutex);
 auto effect=new PadEffect(kind,player,std::move(target));
 if(params){
  const HRESULT result=effect->SetParameters(params,DIEP_ALLPARAMS);
  if(FAILED(result)){
   logf("DirectInput effect parameters rejected native=%d size=%lu type_bytes=%lu result=%08lx",
    !!effect->native,params->dwSize,params->cbTypeSpecificParams,result);
   effect->Release();
   return result;
  }
 }
 *out=effect;
 logf("DirectInput effect created native=%d player=%u type=%08lx",!!effect->native,player+1,kind.Data1);
 return DI_OK;
}

#ifdef PADFIX_PLATFORM_ONLY
static bool xinputDevicePath(const wchar_t* path){
 for(;*path;++path)if(!_wcsnicmp(path,L"&IG_",4))return true;
 return false;
}

static int nativePadXInputSlot(DWORD product,bool xinputPath){
 // An XInput capability extension can match VID/PID independently of the
 // DirectInput enumeration order when different controllers are connected.
 struct ExtendedCaps{
  XINPUT_CAPABILITIES caps;
  WORD vendor,product,version,unknown;
  DWORD reserved;
 };
 using GetExtendedCaps=DWORD (WINAPI*)(DWORD,DWORD,DWORD,ExtendedCaps*);
 static HMODULE xi=systemDll("xinput1_4.dll");
 static auto extended=xi?(GetExtendedCaps)GetProcAddress(xi,MAKEINTRESOURCEA(108)):nullptr;
 bool occupied[4]={};
 for(const auto& entry:nativePadRumble)if(entry.second->slot>=0)occupied[entry.second->slot]=true;
 int fallback=-1;
 for(int slot=0;slot<4;++slot){
  if(occupied[slot]||!xinputConnected(slot))continue;
  ExtendedCaps caps={};
  if(extended&&extended(1,(DWORD)slot,0,&caps)==ERROR_SUCCESS&&caps.vendor&&caps.product){
   if(product==(DWORD)MAKELONG(caps.vendor,caps.product))return slot;
   if(LOWORD(product)!=caps.vendor)continue;
  }
  if(fallback<0&&(xinputPath||LOWORD(product)==0x045e))fallback=slot;
 }
 return fallback;
}

static HRESULT createNativePadEffect(void* device,bool wide,REFGUID kind,LPCDIEFFECT params,
 LPDIRECTINPUTEFFECT* out,LPUNKNOWN outer,HRESULT nativeResult){
 if(SUCCEEDED(nativeResult)||!out||outer||!supportsEffect(kind))return nativeResult;
 std::lock_guard<std::recursive_mutex> lock(mutex);
 auto existing=nativePadRumble.find(device);
 if(existing!=nativePadRumble.end())return createPadEffect(kind,params,out,outer,0,existing->second);
 const DWORD product=nativePadProduct(device,wide);
 DIPROPGUIDANDPATH path={};
 const bool havePath=getHidPath(device,path);
 auto target=std::make_shared<NativePadRumble>();
 if(product&&isSonyRumblePad(product)){
  if(!havePath||!target->hid.open(path.wszPath,product))return nativeResult;
 }else{
  target->slot=nativePadXInputSlot(product,havePath&&xinputDevicePath(path.wszPath));
  // Some Steam/Input virtual devices report a zero or non-Microsoft product
  // while still routing their force feedback to the first XInput slot.
  if(target->slot<0&&havePath&&xinputDevicePath(path.wszPath))
   target->slot=nativePadXInputSlot(0,true);
  if(target->slot<0)return nativeResult;
 }
 const HRESULT result=createPadEffect(kind,params,out,outer,0,target);
 if(SUCCEEDED(result)){
  nativePadRumble.emplace(device,target);
  logf("Native DirectInput force feedback bridged VIDPID=%08lx slot=%d prior=%08lx",
   product,target->slot,nativeResult);
 }
 return result;
}

static void stopNativePadRumble(void* device){
 std::lock_guard<std::recursive_mutex> lock(mutex);
 const auto entry=nativePadRumble.find(device);
 if(entry==nativePadRumble.end())return;
 for(auto effect:padEffects)if(effect->native==entry->second)effect->running=false;
 entry->second->send(0);
}

static void releaseNativePadRumble(void* device){
 std::lock_guard<std::recursive_mutex> lock(mutex);
 const auto entry=nativePadRumble.find(device);
 if(entry==nativePadRumble.end())return;
 stopNativePadRumble(device);
 auto& target=*entry->second;
 target.connected=false;
 target.hid.clear();
 target.hid.close();
 nativePadRumble.erase(entry);
}

static HRESULT nativePadRumbleCommand(void* device,DWORD command,HRESULT nativeResult){
 std::lock_guard<std::recursive_mutex> lock(mutex);
 const auto entry=nativePadRumble.find(device);
 if(entry==nativePadRumble.end())return nativeResult;
 auto& target=*entry->second;
 switch(command){
  case DISFFC_STOPALL:
   stopNativePadRumble(device);
   break;
  case DISFFC_RESET:
   stopNativePadRumble(device);
   for(auto effect:padEffects)if(effect->native==entry->second)effect->downloaded=false;
   target.gain=10000;
   target.paused=false;
   target.enabled=true;
   break;
  case DISFFC_PAUSE:
   if(!target.paused)target.pauseAt=GetTickCount64();
   target.paused=true;
   break;
  case DISFFC_CONTINUE:
   if(target.paused){
    const ULONGLONG duration=GetTickCount64()-target.pauseAt;
    for(auto effect:padEffects)if(effect->native==entry->second&&effect->running)effect->started+=duration;
   }
   target.paused=false;
   break;
  case DISFFC_SETACTUATORSON:target.enabled=true;break;
  case DISFFC_SETACTUATORSOFF:target.enabled=false;break;
  default:return nativeResult;
 }
 updateRumble();
 return DI_OK;
}

static HRESULT nativePadRumbleProperty(void* device,REFGUID prop,LPCDIPROPHEADER params,HRESULT nativeResult){
 if((&prop!=&DIPROP_FFGAIN&&&prop!=&DIPROP_AUTOCENTER)||!params||params->dwSize!=sizeof(DIPROPDWORD))return nativeResult;
 std::lock_guard<std::recursive_mutex> lock(mutex);
 const auto entry=nativePadRumble.find(device);
 if(entry==nativePadRumble.end())return nativeResult;
 if(&prop==&DIPROP_FFGAIN){
  entry->second->gain=std::min(reinterpret_cast<const DIPROPDWORD*>(params)->dwData,10000UL);
  updateRumble();
 }
 return DI_OK;
}
#endif
