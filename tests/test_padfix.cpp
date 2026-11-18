#define PADFIX_TEST
#include "../src/padfix.cpp"
#include <cstdlib>
static XINPUT_STATE fakeState={};static bool online=true;static DWORD fakeSlot=0;
static DWORD WINAPI fakeX(DWORD index,XINPUT_STATE* s){if(!online||index!=fakeSlot)return ERROR_DEVICE_NOT_CONNECTED;*s=fakeState;return ERROR_SUCCESS;}
static int checks=0;
static void check(bool yes,const char* msg){++checks;if(!yes){fprintf(stderr,"FAIL: %s\n",msg);exit(1);}}
static BOOL CALLBACK enumCount(const DIDEVICEINSTANCEA*,void* p){++*(int*)p;return DIENUM_CONTINUE;}
static BOOL CALLBACK objectCount(const DIDEVICEOBJECTINSTANCEA*,void* p){++*(int*)p;return DIENUM_CONTINUE;}
static void resetGate(){armed=false;neutralPending=false;wasForeground=false;fakeState={};sample();sample();}
static DIJOYSTATE2 read(Device& d){DIJOYSTATE2 s;memset(&s,0xcc,sizeof(s));check(d.GetDeviceState(sizeof(s),&s)==DI_OK,"read state");return s;}
int main(){
 logging=false;foregroundOnly=0;getXState=fakeX;neutralMs=0;controllerTakeover=0;
 NativeAction menuActions[16]={},playerActions[21]={};
 const int menuArrows[]={DIK_UP,DIK_DOWN,DIK_LEFT,DIK_RIGHT};
 for(int i=0;i<16;i++){menuActions[i].flags=1;menuActions[i].binding[0].device=8;menuActions[i].binding[1].device=10;}
 for(int i=0;i<4;i++)menuActions[i].binding[0].object=menuArrows[i];
 // Reproduce the user's player map: up-arrow moves left, right-arrow moves forward.
 playerActions[0].binding[0].object=DIK_RIGHT;playerActions[1].binding[0].object=DIK_DOWN;
 playerActions[2].binding[0].object=DIK_UP;playerActions[3].binding[0].object=DIK_LEFT;
 NativeAction originalPlayer[21],originalMenu[16];memcpy(originalPlayer,playerActions,sizeof(playerActions));memcpy(originalMenu,menuActions,sizeof(menuActions));
 NativeMap menu={};menu.actions=menuActions;menu.count=16;bool changed=false;
 check(patchMenuMap(menu,changed)&&changed,"install separate native menu Dpad bindings");
 for(int i=0;i<4;i++){
  check(!memcmp(&menuActions[i].binding[0],&originalMenu[i].binding[0],sizeof(NativeBinding)),"preserve real menu keyboard mapping");
  check(menuActions[i].binding[1].device==0&&!menuActions[i].binding[1].axis&&menuActions[i].binding[1].object==12+i,"menu receives distinct Dpad button, no analog axis");
 }
 check(!memcmp(originalPlayer,playerActions,sizeof(playerActions)),"custom player direction map remains unchanged");
 check(!memcmp(menuActions+4,originalMenu+4,12*sizeof(NativeAction)),"non-direction menu actions remain unchanged");
 check(patchMenuMap(menu,changed)&&!changed,"menu patch idempotent");
 menuActions[3].binding[1].device=2;NativeAction customMenu[16];memcpy(customMenu,menuActions,sizeof(menuActions));
 check(!patchMenuMap(menu,changed)&&!changed&&!memcmp(customMenu,menuActions,sizeof(menuActions)),"custom occupied menu binding refused without partial mutation");
 menu.count=21;check(!patchMenuMap(menu,changed),"refuse player map layout");
 DIJOYSTATE2 ds={};for(auto& p:ds.rgdwPOV)p=0xffffffff;
 XINPUT_STATE normalized;ds4ToXbox(ds,normalized);check(normalized.Gamepad.wButtons==0,"DS4 neutral POV");
 const int sony[]={1,2,0,3,4,5,8,9,10,11};
 for(int i=0;i<10;i++){memset(ds.rgbButtons,0,sizeof(ds.rgbButtons));ds.rgbButtons[sony[i]]=0x80;ds4ToXbox(ds,normalized);check(normalized.Gamepad.wButtons==buttonMasks[i],"DS4 face/shoulder/system button conversion");}
 memset(ds.rgbButtons,0,sizeof(ds.rgbButtons));ds.rgbButtons[6]=ds.rgbButtons[7]=0x80;ds4ToXbox(ds,normalized);
 check(normalized.Gamepad.bLeftTrigger==255&&normalized.Gamepad.bRightTrigger==255,"DS4 triggers use digital buttons, no resting analog phantom");
 const WORD hats[]={1,9,8,10,2,6,4,5};for(int i=0;i<8;i++){ds={};ds.rgdwPOV[0]=i*4500;ds4ToXbox(ds,normalized);check(normalized.Gamepad.wButtons==hats[i],"DS4 eight-way POV conversion");}
 ds.rgdwPOV[0]=0x0000ffff;ds4ToXbox(ds,normalized);check(!normalized.Gamepad.wButtons,"DS4 16-bit neutral POV");
 ds={};ds.rgdwPOV[0]=0xffffffff;ds.lZ=-32767;ds.lRz=32767;ds4ToXbox(ds,normalized);
 check(normalized.Gamepad.sThumbRX==-32767&&normalized.Gamepad.sThumbRY==-32767,"DS4 Z/Rz signed right axes");
 Device d;check(d.SetDataFormat(&c_dfDIJoystick2)==DI_OK,"standard joystick2 format");
 check(d.Acquire()==DI_OK,"acquire");resetGate();
 auto s=read(d);check(s.lX==32767&&s.lY==32767,"neutral axis midpoint");
 check(s.rgdwPOV[0]==0xffffffff&&s.rgdwPOV[3]==0xffffffff,"neutral POV is -1");
 check(std::all_of(std::begin(s.rgbButtons),std::end(s.rgbButtons),[](BYTE b){return !b;}),"neutral buttons");
 DIDEVCAPS caps={sizeof(caps)};check(d.GetCapabilities(&caps)==DI_OK&&caps.dwAxes==2&&caps.dwButtons==20&&caps.dwPOVs==0,"capabilities");
 int n=0;check(d.EnumObjects(objectCount,&n,DIDFT_ALL)==DI_OK&&n==22,"enumerate 2 axes 20 buttons");
 n=0;check(d.EnumObjects(objectCount,&n,DIDFT_AXIS)==DI_OK&&n==2,"axis enumeration");
 n=0;check(d.EnumObjects(objectCount,&n,DIDFT_BUTTON)==DI_OK&&n==20,"button enumeration");
 for(int i=0;i<10;i++){fakeState={};fakeState.Gamepad.wButtons=buttonMasks[i];s=read(d);check(s.rgbButtons[i]==0x80,"Xbox face/shoulder/start buttons");}
 const WORD masks[4]={1,2,4,8};for(int i=0;i<4;i++){fakeState={};fakeState.Gamepad.wButtons=masks[i];s=read(d);check(s.rgbButtons[12+i]==0x80,"Dpad four independently bindable buttons");check(sample().dpad==masks[i],"Dpad keyboard source");}
 fakeState={};fakeState.Gamepad.wButtons=1|8;s=read(d);check(s.rgbButtons[12]&&s.rgbButtons[15],"Dpad diagonal");
 for(int i=0;i<4;i++){fakeState={};if(i<2)fakeState.Gamepad.sThumbRY=i==0?32767:-32768;else fakeState.Gamepad.sThumbRX=i==2?-32768:32767;s=read(d);check(s.rgbButtons[16+i]==0x80,"right stick four independently bindable buttons");check(s.lRx==0&&s.lRy==0&&s.lZ==0,"no native right/trigger axes");}
 fakeState={};fakeState.Gamepad.bLeftTrigger=255;s=read(d);check(s.rgbButtons[10]&&!s.rgbButtons[11],"left trigger independent");
 fakeState.Gamepad.bRightTrigger=255;s=read(d);check(s.rgbButtons[10]&&s.rgbButtons[11],"both triggers independent");
 fakeState={};fakeState.Gamepad.sThumbLX=1000;s=read(d);check(s.lX==32767,"left stick deadzone");
 DIPROPRANGE range={{sizeof(range),sizeof(DIPROPHEADER),0,DIPH_DEVICE},-255,255};
 check(d.SetProperty(DIPROP_RANGE,&range.diph)==DI_OK,"game uses signed -255..255 range");
 fakeState={};s=read(d);check(s.lX==0&&s.lY==0,"signed neutral");
 fakeState.Gamepad.sThumbLX=-32768;fakeState.Gamepad.sThumbLY=32767;s=read(d);check(s.lX==-255&&s.lY==-255,"signed negative extremes");
 fakeState.Gamepad.sThumbLX=32767;fakeState.Gamepad.sThumbLY=-32768;s=read(d);check(s.lX==255&&s.lY==255,"signed positive extremes");
 online=false;s=read(d);check(s.lX==0&&s.lY==0&&!s.rgbButtons[0]&&s.rgdwPOV[0]==0xffffffff,"disconnect neutral");
 online=true;fakeState={};fakeState.Gamepad.wButtons=XINPUT_GAMEPAD_A;s=read(d);check(!s.rgbButtons[0],"reconnect held A suppressed");
 fakeState={};read(d);fakeState.Gamepad.wButtons=XINPUT_GAMEPAD_A;s=read(d);check(s.rgbButtons[0]==0x80,"press A after release");
 DIPROPDWORD buffer={{sizeof(buffer),sizeof(DIPROPHEADER),0,DIPH_DEVICE},32};
 check(d.SetProperty(DIPROP_BUFFERSIZE,&buffer.diph)==DI_OK,"buffer size");
 fakeState={};DIDEVICEOBJECTDATA ev[32];DWORD count=32;check(d.GetDeviceData(sizeof(ev[0]),ev,&count,0)==DI_OK,"buffered neutral");
 fakeState.Gamepad.wButtons=XINPUT_GAMEPAD_A;count=32;check(d.GetDeviceData(sizeof(ev[0]),ev,&count,DIGDD_PEEK)==DI_OK&&count==1&&ev[0].dwOfs==DIJOFS_BUTTON(0)&&ev[0].dwData==0x80,"buffered button event peek");
 count=32;check(d.GetDeviceData(sizeof(ev[0]),ev,&count,0)==DI_OK&&count==1,"peek preserves event");
 fakeState={};count=32;check(d.GetDeviceData(sizeof(ev[0]),ev,&count,0)==DI_OK&&count==1&&ev[0].dwData==0,"buffered release");
 check(d.Unacquire()==DI_OK,"unacquire");count=32;check(d.GetDeviceData(sizeof(ev[0]),ev,&count,0)==DIERR_NOTACQUIRED&&count==0,"unacquired count cleared");
 // Exercise the exact game data format, including wildcard instances, if supplied.
 // Also validate the real system DirectInput delegation in-process without launching the game.
 HMODULE sys=systemDll("dinput8.dll");auto create=(decltype(&DirectInput8Create))GetProcAddress(sys,"DirectInput8Create");
 IDirectInput8A* real=nullptr;check(create(GetModuleHandleA(nullptr),0x800,IID_IDirectInput8A,(void**)&real,nullptr)==DI_OK,"real DirectInput creation");
 Input input(real);n=0;check(input.EnumDevices(DI8DEVCLASS_GAMECTRL,enumCount,&n,DIEDFL_ATTACHEDONLY)==DI_OK&&n==1,"single synthetic controller HRESULT zero");
 IDirectInputDevice8A* pad=nullptr;check(input.CreateDevice(padGuid,&pad,nullptr)==DI_OK&&pad,"synthetic create");
 check(pad->SetDataFormat(&c_dfDIJoystick2)==DI_OK,"synthetic standard format");pad->Release();
 IDirectInputDevice8A* keyboardDevice=nullptr;check(input.CreateDevice(GUID_SysKeyboard,&keyboardDevice,nullptr)==DI_OK&&keyboardDevice,"keyboard delegation");
 check(keyboardDevice->SetDataFormat(&c_dfDIKeyboard)==DI_OK,"keyboard data format passes through");keyboardDevice->Release();
 HWND w=CreateWindowExA(0,"STATIC","PadFix test",0,0,0,1,1,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);
 check(w!=nullptr,"test window");
 check(input.CreateDevice(GUID_SysKeyboard,&keyboardDevice,nullptr)==DI_OK,"create keyboard for actual event tests");
 check(keyboardDevice->SetDataFormat(&c_dfDIKeyboard)==DI_OK,"keyboard format");
 check(keyboardDevice->SetCooperativeLevel(w,DISCL_BACKGROUND|DISCL_NONEXCLUSIVE)==DI_OK,"background keyboard test");
 check(keyboardDevice->SetProperty(DIPROP_BUFFERSIZE,&buffer.diph)==DI_OK,"keyboard buffered mode");
 check(keyboardDevice->Acquire()==DI_OK,"acquire real keyboard");
 fakeState={};BYTE keys[256];check(keyboardDevice->GetDeviceState(256,keys)==DI_OK,"read real keyboard");
 for(int k=0;k<4;k++) {
  fakeState={};fakeState.Gamepad.wButtons=1<<k;const BYTE arrows[]={DIK_UP,DIK_DOWN,DIK_LEFT,DIK_RIGHT};
  check(keyboardDevice->GetDeviceState(256,keys)==DI_OK&&!(keys[arrows[k]]&0x80),"Dpad must not synthesize keyboard arrows");
 }
 fakeState={};count=32;keyboardDevice->GetDeviceData(sizeof(ev[0]),ev,&count,0);
 fakeState.Gamepad.wButtons=1;count=32;
 check(keyboardDevice->GetDeviceData(sizeof(ev[0]),ev,&count,DIGDD_PEEK)==DI_OK,"keyboard buffered passthrough peek");
 bool found=false;for(DWORD j=0;j<count;j++)if(ev[j].dwOfs==DIK_UP&&ev[j].dwData==0x80)found=true;
 check(!found,"Dpad must not synthesize buffered keyboard press");count=32;
 keyboardDevice->GetDeviceData(sizeof(ev[0]),ev,&count,0);
 fakeState={};count=32;keyboardDevice->GetDeviceData(sizeof(ev[0]),ev,&count,0);
 found=false;for(DWORD j=0;j<count;j++)if(ev[j].dwOfs==DIK_UP&&ev[j].dwData==0)found=true;
 check(!found,"Dpad must not synthesize buffered keyboard release");
 keyboardDevice->Unacquire();keyboardDevice->Release();DestroyWindow(w);
 neutralMs=20;resetGate();check(!armed,"neutral time delay");Sleep(25);sample();check(armed,"neutral time elapsed arms controller");
 foregroundOnly=1;check(!foreground(),"test is background process");fakeState.Gamepad.wButtons=XINPUT_GAMEPAD_A;
 check(!sample().buttons[0]&&!armed,"foreground loss clears button and arming");foregroundOnly=0;
 check(!sample().buttons[0],"focus regain held button remains blocked");
 neutralMs=0;fakeState={};online=false;lastConnection=0;sample();
 check(selectedBackend==0&&selectedSlot==-1,"unplug timeout releases automatic backend and slot");
 online=true;fakeSlot=2;sample();sample();check(selectedBackend==1&&selectedSlot==2,"hotplug controller reappearing at a different Xbox slot");
 selectedBackend=2;lastConnection=0;sample();sample();sample();check(selectedBackend==1&&selectedSlot==2,"automatic backend recovers from disconnected DS4 to connected Xbox");
 slotConfig=0;lastConnection=0;auto unplugged=sample();check(selectedSlot==0&&!unplugged.buttons[0],"explicit configured Xbox slot is respected");slotConfig=-1;
 real->Release();
 printf("PASS: %d checks (mock Xbox state + real x86 DirectInput delegation)\n",checks);return 0;
}
