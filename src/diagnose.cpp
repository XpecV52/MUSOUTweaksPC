#define DIRECTINPUT_VERSION 0x0800
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dinput.h>
#include <Xinput.h>
#include <cstdio>
static GUID selected;
static int devices;
static BOOL CALLBACK countPad(const DIDEVICEINSTANCEA* i,void*){printf("  %s type=%lu\n",i->tszInstanceName,i->dwDevType);selected=i->guidInstance;++devices;return DIENUM_CONTINUE;}
int main(){
 puts("OrochiZPadX x86 diagnostic (no game launch, no input injection)");
 char path[MAX_PATH];GetSystemDirectoryA(path,MAX_PATH);strcat_s(path,"\\xinput1_4.dll");HMODULE x=LoadLibraryA(path);
 auto get=(DWORD(WINAPI*)(DWORD,XINPUT_STATE*))GetProcAddress(x,"XInputGetState");
 int live=0;for(DWORD i=0;i<4;i++){XINPUT_STATE s={};DWORD hr=get?get(i,&s):ERROR_PROC_NOT_FOUND;printf("XInput slot %lu: %s (code %lu)\n",i,hr==0?"CONNECTED":"offline",hr);if(!hr)++live;}
 GetModuleFileNameA(nullptr,path,MAX_PATH);char* slash=strrchr(path,'\\');if(slash)slash[1]=0;strcat_s(path,"dinput8.dll");
 HMODULE dll=LoadLibraryA(path);if(!dll){printf("DLL load failed %lu\n",GetLastError());return 1;}
 auto create=(HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,void**,LPUNKNOWN))GetProcAddress(dll,"DirectInput8Create");
 if(!create){puts("Missing DirectInput8Create export");return 1;}
 IDirectInput8A* di=nullptr;HRESULT hr=create(GetModuleHandleA(nullptr),0x800,IID_IDirectInput8A,(void**)&di,nullptr);
 printf("DirectInput8Create HRESULT=%08lx\n",hr);if(FAILED(hr)||!di)return 1;
 hr=di->EnumDevices(DI8DEVCLASS_GAMECTRL,countPad,nullptr,DIEDFL_ATTACHEDONLY);
 printf("EnumDevices HRESULT=%08lx count=%d\n",hr,devices);if(hr!=DI_OK||devices!=1)return 1;
 IDirectInputDevice8A* pad=nullptr;hr=di->CreateDevice(selected,&pad,nullptr);if(FAILED(hr)||!pad)return 1;
 DIDEVCAPS caps={sizeof(caps)};hr=pad->GetCapabilities(&caps);printf("caps HRESULT=%08lx axes=%lu buttons=%lu POVs=%lu\n",hr,caps.dwAxes,caps.dwButtons,caps.dwPOVs);
 if(FAILED(hr))return 1;
 hr=pad->SetDataFormat(&c_dfDIJoystick2);if(FAILED(hr))return 1;pad->Acquire();DIJOYSTATE2 state={};
 for(int tick=0;tick<40;tick++){hr=pad->GetDeviceState(sizeof(state),&state);Sleep(10);}
 unsigned buttons=0;for(int b=0;b<20;b++)if(state.rgbButtons[b]&0x80)buttons|=1<<b;
 printf("state HRESULT=%08lx X=%ld Y=%ld buttons=%05x POV0=%08lx (neutral=ffffffff)\n",hr,state.lX,state.lY,buttons,state.rgdwPOV[0]);pad->Release();di->Release();
 printf("DLL interface: %s; XInput: %s\n",SUCCEEDED(hr)?"PASS":"FAIL",live?"online":"offline (native DS4 can still work)");
 puts("Native DS4 detection and connection status are recorded in OrochiZPadX.log.");
 return FAILED(hr)?1:0;
}
