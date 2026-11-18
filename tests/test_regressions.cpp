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
 monitorBounds={10,20,1930,1100};WINDOWPOS pos={};pos.hwndInsertAfter=HWND_TOP;pos.flags=SWP_NOMOVE|SWP_NOSIZE;
 constrainBorderlessPosition(&pos);check(pos.hwndInsertAfter==HWND_TOP&&!(pos.flags&SWP_NOZORDER),"taskbar activation preserves requested Z-order");
 check(pos.x==10&&pos.y==20&&pos.cx==1920&&pos.cy==1080,"borderless monitor geometry retained");
 pos.flags=SWP_NOZORDER;pos.hwndInsertAfter=HWND_BOTTOM;constrainBorderlessPosition(&pos);check(pos.flags&SWP_NOZORDER,"NOZORDER preserved");
 pos.flags=0;pos.hwndInsertAfter=HWND_TOPMOST;constrainBorderlessPosition(&pos);check(pos.hwndInsertAfter==HWND_TOP,"topmost becomes ordinary top");
 printf("PASS: %d controller/window regression checks\n",checks);return 0;
}
