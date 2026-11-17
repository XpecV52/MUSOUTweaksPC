#define PADFIX_TEST
#include "../src/padfix.cpp"
#include <thread>
#include <cstdlib>
static int checks=0,destroyed=0;
static void check(bool v,const char* name){checks++;if(!v){fprintf(stderr,"FAIL %s\n",name);exit(1);}printf("ok %s\n",name);}
static LRESULT CALLBACK testWindow(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_DESTROY)destroyed++;return DefWindowProcA(h,m,w,l);}
int main(){
 setvbuf(stdout,nullptr,_IONBF,0);
 AddVectoredExceptionHandler(1,[](EXCEPTION_POINTERS* e)->LONG{
  if(e->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION){
   auto c=e->ContextRecord;fprintf(stderr,"AV eip=%08lx eax=%08lx ecx=%08lx edx=%08lx esp=%08lx mode=%lu target=%p\n",c->Eip,c->Eax,c->Ecx,c->Edx,c->Esp,(DWORD)e->ExceptionRecord->ExceptionInformation[0],(void*)e->ExceptionRecord->ExceptionInformation[1]);
   auto stack=(DWORD*)c->Esp;for(int i=0;i<8;i++){HMODULE m=nullptr;char file[MAX_PATH]={};GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCSTR)stack[i],&m);if(m)GetModuleFileNameA(m,file,MAX_PATH);fprintf(stderr,"stack %d %08lx module=%s offset=%08lx\n",i,stack[i],file,stack[i]-(DWORD)m);}
  }return EXCEPTION_CONTINUE_SEARCH;
 });
 logging=false;windowMode=1;renderWidth=2560;renderHeight=1440;
 D3DPRESENT_PARAMETERS p={};p.BackBufferWidth=640;p.BackBufferHeight=480;p.Windowed=FALSE;p.FullScreen_RefreshRateInHz=60;p.PresentationInterval=D3DPRESENT_INTERVAL_FOUR;
 forcePresentation(p);check(p.BackBufferWidth==2560&&p.BackBufferHeight==1440,"native resolution presentation");
 check(p.Windowed&&p.FullScreen_RefreshRateInHz==0&&p.PresentationInterval==D3DPRESENT_INTERVAL_ONE,"valid windowed D3D parameters");
 p.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;forcePresentation(p);check(p.PresentationInterval==D3DPRESENT_INTERVAL_IMMEDIATE,"preserve immediate present option");
 windowMode=0;p.Windowed=FALSE;p.BackBufferWidth=640;forcePresentation(p);check(!p.Windowed&&p.BackBufferWidth==640,"display feature off preserves parameters");windowMode=1;
 realCoInitialize=CoInitializeEx;
 std::thread com([]{
  HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);check(hr==S_OK,"fresh STA initialized");
  hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);check(hr==RPC_E_CHANGED_MODE,"reproduce exact STA versus MTA conflict");
  check(finishCoInitialize(nullptr,0,E_OUTOFMEMORY)==E_OUTOFMEMORY,"other COM errors remain errors");
  check(finishCoInitialize(nullptr,0,RPC_E_CHANGED_MODE)==S_FALSE,"generic COM STA retry returns real success");
  CoUninitialize();check(CoInitializeEx(nullptr,0)==RPC_E_CHANGED_MODE,"one original STA reference still held");
  CoUninitialize();check(CoInitializeEx(nullptr,0)==S_OK,"balanced COM reference cleanup permits new MTA");CoUninitialize();
 });com.join();
 WNDCLASSA wc={};wc.lpfnWndProc=testWindow;wc.hInstance=GetModuleHandleA(nullptr);wc.lpszClassName="PadFixPlatformTest";RegisterClassA(&wc);
 HWND h=CreateWindowExA(0,wc.lpszClassName,"Platform test",WS_POPUP,0,0,2560,1440,nullptr,nullptr,wc.hInstance,nullptr);
 check(h!=nullptr,"hidden D3D test window");
 HMODULE mod=systemDll("d3d9.dll");realCreateD3D=(decltype(realCreateD3D))GetProcAddress(mod,"Direct3DCreate9");
 IDirect3D9* d=realCreateD3D(D3D_SDK_VERSION);check(d!=nullptr,"real system Direct3D9");
 gameWindow=h;hookMethod(d,16,(void*)platformCreateDevice);
 p={};p.BackBufferWidth=640;p.BackBufferHeight=480;p.BackBufferFormat=D3DFMT_X8R8G8B8;p.BackBufferCount=1;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=h;p.Windowed=FALSE;p.FullScreen_RefreshRateInHz=60;p.PresentationInterval=D3DPRESENT_INTERVAL_ONE;
 IDirect3DDevice9* device=nullptr;HRESULT hr=d->CreateDevice(0,D3DDEVTYPE_HAL,h,D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&p,&device);
 check(SUCCEEDED(hr)&&device,"real HAL device created through hook in borderless parameters");
 IDirect3DSurface9* back=nullptr;check(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back)==D3D_OK,"read actual backbuffer");
 D3DSURFACE_DESC desc={};back->GetDesc(&desc);back->Release();check(desc.Width==2560&&desc.Height==1440,"actual GPU render resolution is 2560x1440");
 p.BackBufferWidth=640;p.BackBufferHeight=480;p.Windowed=FALSE;p.FullScreen_RefreshRateInHz=60;
 check(device->Reset(&p)==D3D_OK&&p.Windowed&&p.BackBufferWidth==2560,"Reset hook preserves native resolution windowed mode");
 check(device->Release()==0,"hooked device release");check(d->Release()==0&&renderDevices.empty(),"tracked devices cleaned after release");
 gameWindowProc=(WNDPROC)SetWindowLongPtrA(h,GWLP_WNDPROC,(LONG_PTR)platformWindowProcA);
 nextSourceScan=123;SendMessageA(h,WM_DEVICECHANGE,0,0);check(nextSourceScan==0,"device arrival schedules immediate source scan");
 SendMessageA(h,WM_CLOSE,0,0);check(!IsWindow(h)&&destroyed==1,"close bypasses confirmation but runs normal WM_DESTROY exactly once");
 printf("PASS: %d platform checks (real COM apartments, real GPU D3D9, window lifecycle)\n",checks);return 0;
}
