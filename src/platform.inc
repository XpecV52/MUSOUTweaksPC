// Included by padfix.cpp. All hooks are local to this executable's IAT.
#include <d3d9.h>
#include <d3dcompiler.h>
#include <objbase.h>
#include <intrin.h>
#include <unordered_map>
#include <unordered_set>
#include <cwchar>
#include <cctype>
#ifndef PADFIX_PLATFORM_ONLY
extern "C" void sw2ResolutionInitialize(void);
#endif
static void initializeAll();
static void attachExistingMainWindow();
static int sharedInput=1,windowMode=1,imeIsolation=1,comCompatibility=1,quickClose=1;
static int fpsLimit=0;
static int renderWidth=0,renderHeight=0;
static int internalRenderWidth=0,internalRenderHeight=0;
static bool singlePresentMode=true;
static bool ssm7ResolutionPatched=false,cameraTweakActive=false;
static void patchSsm7Resolution();
static void patchSsm7Camera();
static void patchOrochiCamera();
static void pollCameraHotkeys();
static bool nativeResolution=false,scaleToFill=false;
// A third-party overlay (Steam's gameoverlayrenderer, RTSS) can re-wrap the
// device table around our hooks, so the wrapper chain may call us again while
// we are already inside the same hook. Without a guard that becomes unbounded
// recursion and ends as a 0xC00000FD stack overflow inside the game.
static __declspec(thread) int presentReentry=0;
static __declspec(thread) int resetReentry=0;
static __declspec(thread) int drawReentry=0;
static __declspec(thread) int createReentry=0;
struct ReentryGuard {
 int& depth;
 explicit ReentryGuard(int& d):depth(d){++depth;}
 ~ReentryGuard(){--depth;}
};
static void reportReentry(const char* name) {
 static volatile LONG presentLogged=0,resetLogged=0,drawLogged=0,createLogged=0;
 volatile LONG* flag=&drawLogged;
 if(!strcmp(name,"Present"))flag=&presentLogged;
 else if(!strcmp(name,"Reset"))flag=&resetLogged;
 else if(!strcmp(name,"CreateDevice"))flag=&createLogged;
 if(InterlockedCompareExchange(flag,1,0)==0)logf("D3D hook re-entry blocked for %s: an overlay re-wrapped the device table",name);
}
// 0=stretch the complete logical frame; 1=preserve it centered over a stretched full-screen backdrop.
static int scaleAspectMode=0;
// Diagnostic only: installs the Present hook so a flag file can grab one frame.
static bool frameCapture=false;
// 0=point, 1=linear, 2=Catmull-Rom cubic reconstruction.
static int scaleFilter=2;
static char gameExeName[MAX_PATH];
static HWND gameWindow;
static WNDPROC gameWindowProc;
static RECT monitorBounds;
static int earlyHookCount;
static unsigned earlyHookMask;
static bool graphicsPatched=false;
static bool forceNativeBuffer=false;
static bool deferGetProcHook=false;
static bool lateD3DHookAttempted=false;
static void configureMainWindow(HWND h,bool unicode,int x,int y,int width,int height);
static const char* expectedMainWindowClass();
static void startMainWindowWatch();
static bool isSsm6DxExecutable(const char* name) {
 return !_stricmp(name,"SM6TC.exe")||!_stricmp(name,"SM6EN.exe")||!_stricmp(name,"SM6JP.exe");
}
static bool isLegacyHighResolutionExecutable(const char* name) {
 return !_stricmp(name,"ShinSangokumusou3.exe")||!_stricmp(name,"SM4_WIN.exe")||!_stricmp(name,"SS4.exe")||
  !_stricmp(name,"SS4_Plus.exe")||!_stricmp(name,"SS4_Super.exe")||!_stricmp(name,"SS4_Ultima.exe");
}
static bool legacyInternalResolutionEnabled() {
 // These builds need their own engine-resolution, projection and UI patch.
 // A D3D9-only backbuffer override was observed to crop the scene, so refuse it.
 return false;
}
static decltype(&CreateWindowExA) realCreateWindow;
static decltype(&CreateWindowExW) realCreateWindowW;
static decltype(&SetWindowPos) realSetWindowPos;
static decltype(&SetWindowLongA) realSetWindowLong;
static decltype(&SetWindowLongW) realSetWindowLongW;
static decltype(&ClipCursor) realClipCursor;
static decltype(&MessageBoxA) realMessageBox;
static decltype(&MessageBoxW) realMessageBoxW;
static decltype(&CoInitializeEx) realCoInitialize;
static decltype(&CoInitialize) realCoInitializeLegacy;
static IDirect3D9* (WINAPI* realCreateD3D)(UINT);
static FARPROC (WINAPI* realGetProcAddress)(HMODULE,LPCSTR);
static BOOL (WINAPI* disableIme)(DWORD);
static BOOL (WINAPI* associateIme)(HWND,HIMC,DWORD);
struct TableHook {void* release=nullptr;void* query=nullptr;void* addRef=nullptr;std::unordered_map<size_t,void*> methods;std::unordered_map<size_t,void*> hooks;};
static std::mutex tableMutex;
static std::unordered_map<void*,TableHook> tables;
static std::unordered_set<void*> renderDevices;
static std::unordered_map<void*,std::pair<UINT,UINT>> logicalRenderSizes;
static std::unordered_map<void*,std::pair<UINT,UINT>> loggedNativeViewports,loggedNativeScissors;
static std::unordered_map<void*,IDirect3DSurface9*> scaleSurfaces;
static std::unordered_map<void*,IDirect3DSurface9*> aspectBackdropSurfaces;
static std::unordered_map<void*,IDirect3DSwapChain9*> nativeOutputChains;
struct NativeScaleSize {UINT width,height;D3DFORMAT format;};
static std::unordered_map<void*,IDirect3DTexture9*> nativeScaleTextures;
static std::unordered_map<void*,IDirect3DPixelShader9*> nativeScaleShaders;
static std::unordered_map<void*,NativeScaleSize> nativeScaleSizes;
static std::unordered_set<void*> nativeScaleFailed;
static std::unordered_set<void*> nativeOutputDevices,loggedNativeOutputDevices,loggedNativeOutputFailures;
static std::unordered_set<void*> loggedScaleDevices,loggedScaleFailures,loggedScaleSkip;
static std::unordered_set<void*> loggedPrimaryPresentParameters;
static void* originalMethod(void* object,size_t index) {
 void** table=*(void***)object;
 std::lock_guard<std::mutex> lock(tableMutex);auto i=tables.find(table);
 if(i!=tables.end()){
  if(index==2&&i->second.release)return i->second.release;
  auto method=i->second.methods.find(index);if(method!=i->second.methods.end())return method->second;
 }
 // A third-party private-table copy can invoke an inherited wrapper before
 // CreateDevice/Reset has had a chance to register that copied table.
 for(const auto& known:tables){
  const auto& candidate=known.second;
  if(candidate.query!=table[0]||candidate.addRef!=table[1])continue;
  if(index==2&&candidate.release)return candidate.release;
  auto method=candidate.methods.find(index);if(method!=candidate.methods.end())return method->second;
 }
 return nullptr;
}
static D3DTEXTUREFILTERTYPE standardScaleFilter() {return scaleFilter==0?D3DTEXF_POINT:D3DTEXF_LINEAR;}
static void getNativeDisplaySize(UINT& width,UINT& height) {
 width=(UINT)std::max<LONG>(640,monitorBounds.right-monitorBounds.left);
 height=(UINT)std::max<LONG>(360,monitorBounds.bottom-monitorBounds.top);
 HMONITOR monitor=gameWindow?MonitorFromWindow(gameWindow,MONITOR_DEFAULTTOPRIMARY):MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY);
 MONITORINFOEXA info={};info.cbSize=sizeof(info);
 if(GetMonitorInfoA(monitor,(LPMONITORINFO)&info)){
  DEVMODEA mode={};mode.dmSize=sizeof(mode);
  if(EnumDisplaySettingsA(info.szDevice,ENUM_CURRENT_SETTINGS,&mode)&&mode.dmPelsWidth&&mode.dmPelsHeight){
   width=mode.dmPelsWidth;height=mode.dmPelsHeight;return;
  }
  width=(UINT)std::max<LONG>(640,info.rcMonitor.right-info.rcMonitor.left);
  height=(UINT)std::max<LONG>(360,info.rcMonitor.bottom-info.rcMonitor.top);
 }
}
static bool needsNativeOutputChain(const D3DPRESENT_PARAMETERS& p) {
 bool target=!_stricmp(gameExeName,"Launch.exe")||isSsm6DxExecutable(gameExeName);
 // A second swap chain is shown as another present by Steam/RTSS overlays and
 // can miss the game's gamma ramp. Keep one present per frame and scale the
 // completed logical frame into the primary backbuffer instead.
 if(singlePresentMode&&target)return false;
 return ((nativeResolution&&scaleToFill&&target)||legacyInternalResolutionEnabled())&&gameWindow&&p.hDeviceWindow==gameWindow;
}
static void releaseNativeScaler(void* device) {
 IDirect3DTexture9* texture=nullptr;IDirect3DPixelShader9* shader=nullptr;
 {std::lock_guard<std::mutex> lock(tableMutex);auto t=nativeScaleTextures.find(device);if(t!=nativeScaleTextures.end()){texture=t->second;nativeScaleTextures.erase(t);}auto s=nativeScaleShaders.find(device);if(s!=nativeScaleShaders.end()){shader=s->second;nativeScaleShaders.erase(s);}nativeScaleSizes.erase(device);nativeScaleFailed.erase(device);}
 if(texture)texture->Release();if(shader)shader->Release();
}
static void releaseAllNativeScalers() {
 std::vector<IDirect3DTexture9*> textures;std::vector<IDirect3DPixelShader9*> shaders;
 {std::lock_guard<std::mutex> lock(tableMutex);for(auto& item:nativeScaleTextures)textures.push_back(item.second);for(auto& item:nativeScaleShaders)shaders.push_back(item.second);nativeScaleTextures.clear();nativeScaleShaders.clear();nativeScaleSizes.clear();nativeScaleFailed.clear();}
 for(auto* texture:textures)if(texture)texture->Release();for(auto* shader:shaders)if(shader)shader->Release();
}
static IDirect3DPixelShader9* createNativeScaleShader(IDirect3DDevice9* device) {
 HMODULE compiler=LoadLibraryA("d3dcompiler_47.dll");if(!compiler)compiler=LoadLibraryA("d3dcompiler_43.dll");
 if(!compiler){logf("D3D cubic scaler unavailable: D3DCompiler missing");return nullptr;}
 using CompileFn=HRESULT(WINAPI*)(LPCVOID,SIZE_T,LPCSTR,const D3D_SHADER_MACRO*,ID3DInclude*,LPCSTR,LPCSTR,UINT,UINT,ID3DBlob**,ID3DBlob**);
 auto compile=(CompileFn)GetProcAddress(compiler,"D3DCompile");
 if(!compile){FreeLibrary(compiler);logf("D3D cubic scaler unavailable: D3DCompile missing");return nullptr;}
 static const char shaderSource[]=R"(
float2 SourceSize : register(c0);
float2 SourceInvSize : register(c1);
sampler2D Source : register(s0);
float4 main(float2 uv : TEXCOORD0) : COLOR0 {
 float2 p=uv*SourceSize-0.5;
 float2 b=floor(p);float2 f=p-b;float2 f2=f*f;float2 f3=f2*f;
 float4 wx=float4(-0.5*f.x+f2.x-0.5*f3.x,1.0-2.5*f2.x+1.5*f3.x,0.5*f.x+2.0*f2.x-1.5*f3.x,-0.5*f2.x+0.5*f3.x);
 float4 wy=float4(-0.5*f.y+f2.y-0.5*f3.y,1.0-2.5*f2.y+1.5*f3.y,0.5*f.y+2.0*f2.y-1.5*f3.y,-0.5*f2.y+0.5*f3.y);
 float2 o=0.5*SourceInvSize;
 float4 r0=tex2D(Source,(b+float2(-1,-1))*SourceInvSize+o)*wx.x+tex2D(Source,(b+float2(0,-1))*SourceInvSize+o)*wx.y+tex2D(Source,(b+float2(1,-1))*SourceInvSize+o)*wx.z+tex2D(Source,(b+float2(2,-1))*SourceInvSize+o)*wx.w;
 float4 r1=tex2D(Source,(b+float2(-1,0))*SourceInvSize+o)*wx.x+tex2D(Source,(b+float2(0,0))*SourceInvSize+o)*wx.y+tex2D(Source,(b+float2(1,0))*SourceInvSize+o)*wx.z+tex2D(Source,(b+float2(2,0))*SourceInvSize+o)*wx.w;
 float4 r2=tex2D(Source,(b+float2(-1,1))*SourceInvSize+o)*wx.x+tex2D(Source,(b+float2(0,1))*SourceInvSize+o)*wx.y+tex2D(Source,(b+float2(1,1))*SourceInvSize+o)*wx.z+tex2D(Source,(b+float2(2,1))*SourceInvSize+o)*wx.w;
 float4 r3=tex2D(Source,(b+float2(-1,2))*SourceInvSize+o)*wx.x+tex2D(Source,(b+float2(0,2))*SourceInvSize+o)*wx.y+tex2D(Source,(b+float2(1,2))*SourceInvSize+o)*wx.z+tex2D(Source,(b+float2(2,2))*SourceInvSize+o)*wx.w;
 float4 c=r0*wy.x+r1*wy.y+r2*wy.z+r3*wy.w;
 return saturate(c);
})";
 ID3DBlob* code=nullptr;ID3DBlob* errors=nullptr;
 HRESULT hr=compile(shaderSource,sizeof(shaderSource)-1,"MUSOUTweaksPC cubic scale",nullptr,nullptr,"main","ps_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
 if(errors){if(FAILED(hr))logf("D3D cubic shader compile failed: %.*s",(int)errors->GetBufferSize(),(const char*)errors->GetBufferPointer());errors->Release();}
 IDirect3DPixelShader9* shader=nullptr;
 if(SUCCEEDED(hr)&&code)hr=device->CreatePixelShader((const DWORD*)code->GetBufferPointer(),&shader);
 if(code)code->Release();FreeLibrary(compiler);
 if(FAILED(hr)||!shader){logf("D3D cubic shader creation failed hr=%08lx",hr);return nullptr;}
 return shader;
}
static void disableNativeCubicScaler(void* device,const char* stage,HRESULT hr) {
 bool first=false;{std::lock_guard<std::mutex> lock(tableMutex);first=nativeScaleFailed.insert(device).second;}
 if(first)logf("D3D cubic scaler disabled stage=%s hr=%08lx; using linear fallback",stage,hr);
}
static bool acquireNativeScaleResources(IDirect3DDevice9* device,UINT sourceWidth,UINT sourceHeight,D3DFORMAT sourceFormat,IDirect3DTexture9** textureOut,IDirect3DPixelShader9** shaderOut) {
 if(!device||!textureOut||!shaderOut)return false;*textureOut=nullptr;*shaderOut=nullptr;
 std::lock_guard<std::mutex> lock(tableMutex);
 if(nativeScaleFailed.count(device))return false;
 auto size=nativeScaleSizes.find(device);auto texture=nativeScaleTextures.find(device);
 if(texture!=nativeScaleTextures.end()&&(size==nativeScaleSizes.end()||size->second.width!=sourceWidth||size->second.height!=sourceHeight||size->second.format!=sourceFormat)){
  texture->second->Release();nativeScaleTextures.erase(texture);nativeScaleSizes.erase(device);texture=nativeScaleTextures.end();
 }
 if(texture==nativeScaleTextures.end()){
  IDirect3DTexture9* created=nullptr;HRESULT hr=device->CreateTexture(sourceWidth,sourceHeight,1,D3DUSAGE_RENDERTARGET,sourceFormat,D3DPOOL_DEFAULT,&created,nullptr);
  if(FAILED(hr)||!created){nativeScaleFailed.insert(device);logf("D3D cubic source texture creation failed size=%ux%u format=%u hr=%08lx",sourceWidth,sourceHeight,sourceFormat,hr);return false;}
  nativeScaleTextures[device]=created;nativeScaleSizes[device]={sourceWidth,sourceHeight,sourceFormat};texture=nativeScaleTextures.find(device);
 }
 auto shader=nativeScaleShaders.find(device);
 if(shader==nativeScaleShaders.end()){
  IDirect3DPixelShader9* created=createNativeScaleShader(device);if(!created){nativeScaleFailed.insert(device);return false;}
  nativeScaleShaders[device]=created;shader=nativeScaleShaders.find(device);
 }
 *textureOut=texture->second;(*textureOut)->AddRef();*shaderOut=shader->second;(*shaderOut)->AddRef();return true;
}
static bool cubicScaleToOutput(IDirect3DDevice9* device,IDirect3DSurface9* source,IDirect3DSurface9* output,const RECT* sourceRect=nullptr,const RECT* destinationRect=nullptr) {
 D3DSURFACE_DESC src={},dst={};if(FAILED(source->GetDesc(&src))||FAILED(output->GetDesc(&dst))||!src.Width||!src.Height||!dst.Width||!dst.Height)return false;
 RECT crop={0,0,(LONG)src.Width,(LONG)src.Height};if(sourceRect)crop=*sourceRect;
 if(crop.left<0||crop.top<0||crop.right>(LONG)src.Width||crop.bottom>(LONG)src.Height||crop.right<=crop.left||crop.bottom<=crop.top)return false;
 RECT target={0,0,(LONG)dst.Width,(LONG)dst.Height};if(destinationRect)target=*destinationRect;
 if(target.left<0||target.top<0||target.right>(LONG)dst.Width||target.bottom>(LONG)dst.Height||target.right<=target.left||target.bottom<=target.top)return false;
 UINT sourceWidth=(UINT)(crop.right-crop.left),sourceHeight=(UINT)(crop.bottom-crop.top);
 IDirect3DTexture9* texture=nullptr;IDirect3DPixelShader9* shader=nullptr;
 if(!acquireNativeScaleResources(device,sourceWidth,sourceHeight,src.Format,&texture,&shader))return false;
 IDirect3DSurface9* textureSurface=nullptr;HRESULT copyHr=texture->GetSurfaceLevel(0,&textureSurface);
 if(SUCCEEDED(copyHr))copyHr=device->StretchRect(source,&crop,textureSurface,nullptr,D3DTEXF_NONE);
 if(textureSurface)textureSurface->Release();
 if(FAILED(copyHr)){disableNativeCubicScaler(device,"source copy",copyHr);texture->Release();shader->Release();return false;}
 IDirect3DStateBlock9* states=nullptr;HRESULT stateHr=device->CreateStateBlock(D3DSBT_ALL,&states);
 if(FAILED(stateHr)||!states){if(states)states->Release();texture->Release();shader->Release();disableNativeCubicScaler(device,"state block creation",stateHr);return false;}
 stateHr=states->Capture();if(FAILED(stateHr)){states->Release();texture->Release();shader->Release();disableNativeCubicScaler(device,"state capture",stateHr);return false;}
 IDirect3DSurface9* previousTarget=nullptr;IDirect3DSurface9* previousDepth=nullptr;
 HRESULT targetHr=device->GetRenderTarget(0,&previousTarget);
 HRESULT drawHr=FAILED(targetHr)?targetHr:S_OK;
 const char* failedStage="setup";
 if(FAILED(targetHr)){if(previousTarget)previousTarget->Release();device->GetDepthStencilSurface(&previousDepth);if(previousDepth)previousDepth->Release();states->Release();texture->Release();shader->Release();disableNativeCubicScaler(device,"render target query",targetHr);return false;}
 device->GetDepthStencilSurface(&previousDepth);
 if(SUCCEEDED(drawHr)){drawHr=device->SetRenderTarget(0,output);if(FAILED(drawHr))failedStage="SetRenderTarget";}
 if(SUCCEEDED(drawHr)){drawHr=device->SetDepthStencilSurface(nullptr);if(FAILED(drawHr))failedStage="SetDepthStencilSurface";}
 if(SUCCEEDED(drawHr)){
  D3DVIEWPORT9 viewport={0,0,dst.Width,dst.Height,0.0f,1.0f};drawHr=device->SetViewport(&viewport);if(FAILED(drawHr))failedStage="SetViewport";
 }
 if(SUCCEEDED(drawHr)){drawHr=device->SetPixelShader(shader);if(FAILED(drawHr))failedStage="SetPixelShader";}
 if(SUCCEEDED(drawHr)){drawHr=device->SetVertexShader(nullptr);if(FAILED(drawHr))failedStage="SetVertexShader";}
 if(SUCCEEDED(drawHr)){drawHr=device->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);if(FAILED(drawHr))failedStage="SetFVF";}
 if(SUCCEEDED(drawHr)){drawHr=device->SetTexture(0,texture);if(FAILED(drawHr))failedStage="SetTexture";}
 if(SUCCEEDED(drawHr)){
  float size[4]={(float)sourceWidth,(float)sourceHeight,0.0f,0.0f};
  float inverse[4]={1.0f/(float)sourceWidth,1.0f/(float)sourceHeight,0.0f,0.0f};
  drawHr=device->SetPixelShaderConstantF(0,size,1);if(FAILED(drawHr))failedStage="SetPixelShaderConstantF0";if(SUCCEEDED(drawHr)){drawHr=device->SetPixelShaderConstantF(1,inverse,1);if(FAILED(drawHr))failedStage="SetPixelShaderConstantF1";}
 }
 if(SUCCEEDED(drawHr)){
  device->SetRenderState(D3DRS_ZENABLE,FALSE);device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);device->SetRenderState(D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_RED|D3DCOLORWRITEENABLE_GREEN|D3DCOLORWRITEENABLE_BLUE|D3DCOLORWRITEENABLE_ALPHA);
  device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);device->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);device->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);device->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);device->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
  struct Vertex {float x,y,z,rhw,u,v;};float left=(float)target.left-0.5f,top=(float)target.top-0.5f,right=(float)target.right-0.5f,bottom=(float)target.bottom-0.5f;
  Vertex quad[4]={{left,top,0.0f,1.0f,0.0f,0.0f},{right,top,0.0f,1.0f,1.0f,0.0f},{left,bottom,0.0f,1.0f,0.0f,1.0f},{right,bottom,0.0f,1.0f,1.0f,1.0f}};
  drawHr=device->BeginScene();if(FAILED(drawHr))failedStage="BeginScene";
  if(SUCCEEDED(drawHr)){drawHr=device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex));if(FAILED(drawHr))failedStage="DrawPrimitiveUP";HRESULT endHr=device->EndScene();if(SUCCEEDED(drawHr)&&FAILED(endHr)){drawHr=endHr;failedStage="EndScene";}}
 }
 if(states){states->Apply();states->Release();}
 if(previousTarget){device->SetRenderTarget(0,previousTarget);previousTarget->Release();}
 if(previousDepth){device->SetDepthStencilSurface(previousDepth);previousDepth->Release();}else device->SetDepthStencilSurface(nullptr);
 texture->Release();shader->Release();
 if(FAILED(drawHr))disableNativeCubicScaler(device,failedStage,drawHr);
 return SUCCEEDED(drawHr);
}
static void releaseNativeOutputChain(void* device) {
 IDirect3DSwapChain9* chain=nullptr;
 {std::lock_guard<std::mutex> lock(tableMutex);auto i=nativeOutputChains.find(device);if(i!=nativeOutputChains.end()){chain=i->second;nativeOutputChains.erase(i);}loggedNativeOutputDevices.erase(device);loggedNativeOutputFailures.erase(device);}
 if(chain)chain->Release();releaseNativeScaler(device);
}
static void releaseAllNativeOutputChains() {
 std::vector<IDirect3DSwapChain9*> chains;
 {std::lock_guard<std::mutex> lock(tableMutex);for(auto& item:nativeOutputChains)chains.push_back(item.second);nativeOutputChains.clear();nativeOutputDevices.clear();loggedNativeOutputDevices.clear();loggedNativeOutputFailures.clear();}
 for(auto* chain:chains)if(chain)chain->Release();
 releaseAllNativeScalers();
}
static bool createNativeOutputChain(IDirect3DDevice9* device) {
 if(!device||!gameWindow)return false;
 UINT width=0,height=0;getNativeDisplaySize(width,height);
 IDirect3DSurface9* source=nullptr;D3DSURFACE_DESC desc={};
 if(FAILED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&source))||!source)return false;
 HRESULT descHr=source->GetDesc(&desc);source->Release();if(FAILED(descHr))return false;
 D3DPRESENT_PARAMETERS p={};p.BackBufferWidth=width;p.BackBufferHeight=height;p.BackBufferFormat=desc.Format;p.BackBufferCount=1;
 p.MultiSampleType=D3DMULTISAMPLE_NONE;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=gameWindow;p.Windowed=TRUE;
 p.EnableAutoDepthStencil=FALSE;p.FullScreen_RefreshRateInHz=0;p.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
 IDirect3DSwapChain9* chain=nullptr;HRESULT hr=device->CreateAdditionalSwapChain(&p,&chain);
 if(FAILED(hr)||!chain){logf("D3D native output chain failed device=%p size=%ux%u format=%u hr=%08lx",device,width,height,desc.Format,hr);return false;}
 IDirect3DSwapChain9* duplicate=nullptr;
 {std::lock_guard<std::mutex> lock(tableMutex);auto i=nativeOutputChains.find(device);if(i==nativeOutputChains.end())nativeOutputChains[device]=chain;else duplicate=i->second;}
 if(duplicate){chain->Release();chain=duplicate;}
 logf("D3D native output chain ready device=%p size=%ux%u format=%u filter=%u",device,width,height,desc.Format,scaleFilter);
 return true;
}
static void releaseScaleSurface(void* object) {
 IDirect3DSurface9* surface=nullptr;IDirect3DSurface9* backdrop=nullptr;
 {std::lock_guard<std::mutex> lock(tableMutex);auto i=scaleSurfaces.find(object);if(i!=scaleSurfaces.end()){surface=i->second;scaleSurfaces.erase(i);}auto b=aspectBackdropSurfaces.find(object);if(b!=aspectBackdropSurfaces.end()){backdrop=b->second;aspectBackdropSurfaces.erase(b);}loggedScaleDevices.erase(object);loggedScaleFailures.erase(object);}
 if(surface)surface->Release();if(backdrop)backdrop->Release();
}
static void releaseAllScaleSurfaces() {
 std::vector<IDirect3DSurface9*> surfaces;
 {std::lock_guard<std::mutex> lock(tableMutex);for(auto& item:scaleSurfaces)surfaces.push_back(item.second);for(auto& item:aspectBackdropSurfaces)surfaces.push_back(item.second);scaleSurfaces.clear();aspectBackdropSurfaces.clear();loggedScaleDevices.clear();loggedScaleFailures.clear();}
 for(auto* surface:surfaces)if(surface)surface->Release();
}
static IDirect3DSurface9* acquireScaleSurface(IDirect3DDevice9* d,UINT width,UINT height,D3DFORMAT format) {
 IDirect3DSurface9* cached=nullptr;
 {std::lock_guard<std::mutex> lock(tableMutex);auto i=scaleSurfaces.find(d);if(i!=scaleSurfaces.end()){cached=i->second;cached->AddRef();}}
 if(cached){D3DSURFACE_DESC desc={};if(SUCCEEDED(cached->GetDesc(&desc))&&desc.Width==width&&desc.Height==height&&desc.Format==format)return cached;cached->Release();releaseScaleSurface(d);}
 IDirect3DSurface9* created=nullptr;HRESULT hr=d->CreateRenderTarget(width,height,format,D3DMULTISAMPLE_NONE,0,FALSE,&created,nullptr);
 if(FAILED(hr)||!created){bool logFailure=false;{std::lock_guard<std::mutex> lock(tableMutex);logFailure=loggedScaleFailures.insert(d).second;}if(logFailure)logf("D3D scale target create failed size=%ux%u format=%u hr=%08lx",width,height,format,hr);return nullptr;}
 IDirect3DSurface9* result=created;
 {std::lock_guard<std::mutex> lock(tableMutex);auto i=scaleSurfaces.find(d);if(i==scaleSurfaces.end()){created->AddRef();scaleSurfaces[d]=created;}else{i->second->AddRef();result=i->second;}}
 if(result!=created)created->Release();
 return result;
}
static IDirect3DSurface9* acquireAspectBackdropSurface(IDirect3DDevice9* d,UINT width,UINT height,D3DFORMAT format) {
 IDirect3DSurface9* cached=nullptr;
 {std::lock_guard<std::mutex> lock(tableMutex);auto i=aspectBackdropSurfaces.find(d);if(i!=aspectBackdropSurfaces.end()){cached=i->second;cached->AddRef();}}
 if(cached){D3DSURFACE_DESC desc={};if(SUCCEEDED(cached->GetDesc(&desc))&&desc.Width==width&&desc.Height==height&&desc.Format==format)return cached;cached->Release();IDirect3DSurface9* stale=nullptr;{std::lock_guard<std::mutex> lock(tableMutex);auto i=aspectBackdropSurfaces.find(d);if(i!=aspectBackdropSurfaces.end()){stale=i->second;aspectBackdropSurfaces.erase(i);}}if(stale)stale->Release();}
 IDirect3DSurface9* created=nullptr;HRESULT hr=d->CreateRenderTarget(width,height,format,D3DMULTISAMPLE_NONE,0,FALSE,&created,nullptr);
 if(FAILED(hr)||!created)return nullptr;
 IDirect3DSurface9* result=created;
 {std::lock_guard<std::mutex> lock(tableMutex);auto i=aspectBackdropSurfaces.find(d);if(i==aspectBackdropSurfaces.end()){created->AddRef();aspectBackdropSurfaces[d]=created;}else{i->second->AddRef();result=i->second;}}
 if(result!=created)created->Release();
 return result;
}
static HRESULT drawAspectBackdrop(IDirect3DDevice9* device,IDirect3DSurface9* source,IDirect3DSurface9* output,UINT sourceW,UINT sourceH,D3DFORMAT format) {
 // Downsample once into a cached low-resolution surface, then stretch the
 // softened whole frame to fill the canvas behind the aspect-correct image.
 UINT blurW=std::max<UINT>(64,sourceW/6),blurH=std::max<UINT>(64,sourceH/6);
 IDirect3DSurface9* blur=acquireAspectBackdropSurface(device,blurW,blurH,format);
 if(!blur)return device->StretchRect(source,nullptr,output,nullptr,D3DTEXF_LINEAR);
 HRESULT hr=device->StretchRect(source,nullptr,blur,nullptr,D3DTEXF_LINEAR);
 if(SUCCEEDED(hr))hr=device->StretchRect(blur,nullptr,output,nullptr,D3DTEXF_LINEAR);
 blur->Release();return hr;
}
static bool scaleBackbufferToNative(IDirect3DDevice9* d,const RECT* source,const RECT* destination) {
 if(!nativeResolution||!scaleToFill)return false;
 UINT logicalW=0,logicalH=0;{std::lock_guard<std::mutex> lock(tableMutex);auto i=logicalRenderSizes.find(d);if(i!=logicalRenderSizes.end()){logicalW=i->second.first;logicalH=i->second.second;}}
 IDirect3DSurface9* back=nullptr;if(FAILED(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back))||!back)return false;
 D3DSURFACE_DESC desc={};HRESULT descHr=back->GetDesc(&desc);
 if(FAILED(descHr)||!desc.Width||!desc.Height){back->Release();return false;}
 RECT crop={0,0,(LONG)desc.Width,(LONG)desc.Height};
 // The viewport the game set for the frame being presented is the only
 // reliable description of where its content actually sits. The tracked
 // CreateDevice/Reset size goes stale as soon as the game adopts a different
 // render size at runtime, and cropping with it cuts a slice out of a
 // correctly rendered frame.
 D3DVIEWPORT9 viewport={};bool haveViewport=SUCCEEDED(d->GetViewport(&viewport))&&viewport.Width&&viewport.Height;
 bool viewportFillsBuffer=haveViewport&&viewport.X==0&&viewport.Y==0&&viewport.Width>=desc.Width&&viewport.Height>=desc.Height;
 UINT outputW=0,outputH=0;getNativeDisplaySize(outputW,outputH);
 // A backbuffer that already matches the output size and is filled by the
 // game itself needs no second scale: resampling it through a stale crop is
 // what destroyed the image on SSM6 DX.
 if(viewportFillsBuffer&&desc.Width==outputW&&desc.Height==outputH){
  bool logSkip=false;{std::lock_guard<std::mutex> lock(tableMutex);logSkip=loggedScaleSkip.insert(d).second;}
  if(logSkip)logf("D3D scale skipped: backbuffer %ux%u is already the output size and the game viewport fills it",desc.Width,desc.Height);
  back->Release();return false;
 }
 bool sourceValid=source&&source->left>=0&&source->top>=0&&source->right<=(LONG)desc.Width&&source->bottom<=(LONG)desc.Height&&source->right>source->left&&source->bottom>source->top;
 // Respect an explicit smaller source rectangle, then the live viewport, and
 // only then the tracked logical size as a last resort.
 if(sourceValid&&((UINT)(source->right-source->left)<desc.Width||(UINT)(source->bottom-source->top)<desc.Height))crop=*source;
 else if(haveViewport&&viewport.X>=0&&viewport.Y>=0&&viewport.X+(LONG)viewport.Width<=(LONG)desc.Width&&viewport.Y+(LONG)viewport.Height<=(LONG)desc.Height&&(viewport.Width<desc.Width||viewport.Height<desc.Height))crop={(LONG)viewport.X,(LONG)viewport.Y,(LONG)viewport.X+(LONG)viewport.Width,(LONG)viewport.Y+(LONG)viewport.Height};
 else if(logicalW&&logicalH&&logicalW<=desc.Width&&logicalH<=desc.Height&&(logicalW<desc.Width||logicalH<desc.Height))crop={0,0,(LONG)logicalW,(LONG)logicalH};
 else if(sourceValid)crop=*source;
 UINT sourceW=(UINT)(crop.right-crop.left),sourceH=(UINT)(crop.bottom-crop.top);
 if(!sourceW||!sourceH||(sourceW==desc.Width&&sourceH==desc.Height)){back->Release();return false;}
 RECT content={0,0,(LONG)desc.Width,(LONG)desc.Height};bool aspectFit=scaleAspectMode==1;
 if(aspectFit){
  uint64_t sourceCross=(uint64_t)sourceW*desc.Height,targetCross=(uint64_t)sourceH*desc.Width;
  if(sourceCross<targetCross){UINT fitW=(UINT)((uint64_t)sourceW*desc.Height/sourceH);fitW=std::clamp<UINT>(fitW,1,desc.Width);content.left=(LONG)((desc.Width-fitW)/2);content.right=content.left+(LONG)fitW;}
  else if(sourceCross>targetCross){UINT fitH=(UINT)((uint64_t)sourceH*desc.Width/sourceW);fitH=std::clamp<UINT>(fitH,1,desc.Height);content.top=(LONG)((desc.Height-fitH)/2);content.bottom=content.top+(LONG)fitH;}
  aspectFit=content.left||content.top||content.right!=(LONG)desc.Width||content.bottom!=(LONG)desc.Height;
 }
 IDirect3DSurface9* temp=acquireScaleSurface(d,sourceW,sourceH,desc.Format);if(!temp){back->Release();return false;}
 HRESULT copyHr=d->StretchRect(back,&crop,temp,nullptr,D3DTEXF_NONE),scaleHr=S_OK;bool cubic=false,backdropReady=!aspectFit;
 if(SUCCEEDED(copyHr)&&aspectFit){scaleHr=drawAspectBackdrop(d,temp,back,sourceW,sourceH,desc.Format);backdropReady=SUCCEEDED(scaleHr);}
 if(SUCCEEDED(copyHr)&&!backdropReady){
  // If a driver rejects the low-resolution render target, keep a full-frame
  // stretched backdrop rather than leaving black bars or edge-column streaks.
  scaleHr=d->StretchRect(temp,nullptr,back,nullptr,standardScaleFilter());backdropReady=SUCCEEDED(scaleHr);
 }
 if(SUCCEEDED(copyHr)&&backdropReady&&scaleFilter==2)cubic=cubicScaleToOutput(d,temp,back,nullptr,aspectFit?&content:nullptr);
 if(SUCCEEDED(copyHr)&&backdropReady&&!cubic)scaleHr=d->StretchRect(temp,nullptr,back,aspectFit?&content:nullptr,standardScaleFilter());
 bool logAttempt=false,logFailure=false;
 {std::lock_guard<std::mutex> lock(tableMutex);logAttempt=loggedScaleDevices.insert(d).second;if(!cubic&&(FAILED(copyHr)||FAILED(scaleHr)))logFailure=loggedScaleFailures.insert(d).second;}
 if(logAttempt||logFailure)logf("D3D primary single-present scale crop=%ld,%ld %ux%u destination=%ux%u filter=%u cubic=%d aspect_mode=%d backdrop=%d content=%ld,%ld-%ld,%ld source_hr=%08lx scale_hr=%08lx present_source=%d present_destination=%d",crop.left,crop.top,sourceW,sourceH,desc.Width,desc.Height,scaleFilter,cubic,scaleAspectMode,backdropReady,content.left,content.top,content.right,content.bottom,copyHr,scaleHr,source!=nullptr,destination!=nullptr);
 if(temp)temp->Release();back->Release();
 return SUCCEEDED(copyHr)&&(cubic||SUCCEEDED(scaleHr));
}
static void pacePresentation() {
 if(fpsLimit<=0)return;
 static std::mutex paceMutex;
 static LARGE_INTEGER frequency={};
 static LONGLONG nextTick=0,remainder=0;
 static LONGLONG measureStart=0;
 static unsigned measureFrames=0;
 static HANDLE timer=nullptr;
 std::lock_guard<std::mutex> lock(paceMutex);
 if(!frequency.QuadPart&&!QueryPerformanceFrequency(&frequency))return;
 if(!timer){
  using CreateTimerFn=HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES,LPCWSTR,DWORD,DWORD);
  auto create=(CreateTimerFn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"CreateWaitableTimerExW");
  // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION (Windows 10 1803+), with an
  // auto-reset timer fallback for earlier systems.
  if(create)timer=create(nullptr,nullptr,0x2,TIMER_ALL_ACCESS);
  if(!timer)timer=CreateWaitableTimerW(nullptr,FALSE,nullptr);
 }
 LARGE_INTEGER now={};QueryPerformanceCounter(&now);
 const LONGLONG period=frequency.QuadPart/fpsLimit;
 if(!nextTick||now.QuadPart-nextTick>period*2){nextTick=now.QuadPart;remainder=0;}
 while(now.QuadPart<nextTick){
  LONGLONG remaining=nextTick-now.QuadPart;
  if(timer&&remaining>frequency.QuadPart/1000){
   LARGE_INTEGER due={};due.QuadPart=-((remaining*10000000LL)/frequency.QuadPart);
   if(due.QuadPart==0)due.QuadPart=-1;
   if(SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE))WaitForSingleObject(timer,INFINITE);
   else SwitchToThread();
  }else SwitchToThread();
  QueryPerformanceCounter(&now);
 }
 if(!measureStart)measureStart=now.QuadPart;
 if(++measureFrames>=300&&now.QuadPart>measureStart){
  double actual=(double)measureFrames*(double)frequency.QuadPart/(double)(now.QuadPart-measureStart);
  logf("D3D9 present pace target=%d actual=%.2f",fpsLimit,actual);
  measureFrames=0;measureStart=now.QuadPart;
 }
 nextTick+=period;remainder+=frequency.QuadPart%fpsLimit;
 if(remainder>=fpsLimit){nextTick++;remainder-=fpsLimit;}
}
static ULONG WINAPI platformRelease(void* object) {
 auto fn=(ULONG(WINAPI*)(void*))originalMethod(object,2);if(!fn||fn==platformRelease)return 0;
 ULONG refs=fn(object);
 if(!refs){releaseScaleSurface(object);releaseNativeOutputChain(object);std::lock_guard<std::mutex> lock(tableMutex);renderDevices.erase(object);logicalRenderSizes.erase(object);nativeOutputDevices.erase(object);loggedNativeViewports.erase(object);loggedNativeScissors.erase(object);loggedPrimaryPresentParameters.erase(object);}
 return refs;
}
static void hookTable(void** table,size_t index,void* hook) {
 if(!table)return;
 // Patch only the requested private vtable slots; never truncate/copy them.
 std::unique_lock<std::mutex> lock(tableMutex);auto& entry=tables[table];
 // An overlay can copy an already hooked private COM table. Never save our
 // wrapper as its own original: that causes unbounded Release/Present recursion.
 // Match the immutable IUnknown methods to find the copied table's originals.
 if(entry.query&&(entry.query!=table[0]||entry.addRef!=table[1]))entry=TableHook{};
 entry.query=table[0];entry.addRef=table[1];
 for(const auto& known:tables){
  const auto& candidate=known.second;
  if(&candidate==&entry||candidate.query!=entry.query||candidate.addRef!=entry.addRef)continue;
  for(const auto& method:candidate.hooks){
   if(table[method.first]!=method.second||entry.methods.count(method.first))continue;
   auto original=candidate.methods.find(method.first);
   if(original!=candidate.methods.end()){
    entry.methods[method.first]=original->second;entry.hooks[method.first]=method.second;
   }
  }
 }
 auto inherited=[&](size_t slot,void* wrapper)->void*{
  for(const auto& known:tables){
   const auto& candidate=known.second;
   if(&candidate==&entry||candidate.query!=entry.query||candidate.addRef!=entry.addRef)continue;
   void* original=nullptr;
   if(slot==2)original=candidate.release;
   else {auto i=candidate.methods.find(slot);if(i!=candidate.methods.end())original=i->second;}
   if(original&&original!=wrapper)return original;
  }
  return nullptr;
 };
 if(!entry.release){
  entry.release=table[2]==(void*)platformRelease?inherited(2,(void*)platformRelease):table[2];
  if(!entry.release){lock.unlock();logf("COM hook skipped: inherited Release original unavailable table=%p",table);return;}
 }
 if(!entry.methods.count(index)){
  void* original=table[index]==hook?inherited(index,hook):table[index];
  if(!original){lock.unlock();logf("COM hook skipped: inherited original unavailable table=%p slot=%u",table,(unsigned)index);return;}
  entry.methods[index]=original;
 }
 entry.hooks[index]=hook;
 DWORD old=0;
 if(VirtualProtect(table+2,sizeof(void*),PAGE_READWRITE,&old)){
  InterlockedExchangePointer(table+2,(void*)platformRelease);DWORD discard;VirtualProtect(table+2,sizeof(void*),old,&discard);
 }
 old=0;if(VirtualProtect(table+index,sizeof(void*),PAGE_READWRITE,&old)){
  InterlockedExchangePointer(table+index,hook);DWORD discard;VirtualProtect(table+index,sizeof(void*),old,&discard);
 }
}
static void hookMethod(void* object,size_t index,void* hook) {
 if(object)hookTable(*(void***)object,index,hook);
}
static void forcePresentation(D3DPRESENT_PARAMETERS& p) {
 if(!windowMode){if(fpsLimit>0)p.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;return;}
 p.Windowed=TRUE;p.FullScreen_RefreshRateInHz=0;
 // The legacy titles use game-specific display setup code but create their
 // final scene through D3D9. They keep their own backbuffer: the DW4 Hyper /
 // DW5 Special profiles refuse every render-size override above.
 if(legacyInternalResolutionEnabled()&&needsNativeOutputChain(p)){
  p.BackBufferWidth=(UINT)internalRenderWidth;p.BackBufferHeight=(UINT)internalRenderHeight;
 }
 // Older Koei games keep their UI viewport at the selected game resolution.
 // Keep that buffer unless the profile explicitly asks for native rendering.
  if(nativeResolution&&!needsNativeOutputChain(p)){
   UINT nativeW=0,nativeH=0;getNativeDisplaySize(nativeW,nativeH);
   // Keep an explicitly higher-than-desktop game render target (for example
   // DW8's 3840x2160 target). The windowed primary swapchain lets DWM scale it
   // to the client area, while retaining the single Present/gamma path.
   if(!singlePresentMode||p.BackBufferWidth<=nativeW||p.BackBufferHeight<=nativeH){p.BackBufferWidth=nativeW;p.BackBufferHeight=nativeH;}
  }
 else if(forceNativeBuffer&&!needsNativeOutputChain(p)){p.BackBufferWidth=renderWidth;p.BackBufferHeight=renderHeight;}
 // An explicit game frame limit owns the cadence; do not add a second wait
 // tied to the desktop refresh rate. Windowed mode leaves the monitor mode intact.
 if(fpsLimit>0)p.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
 else if(p.PresentationInterval!=D3DPRESENT_INTERVAL_IMMEDIATE)p.PresentationInterval=D3DPRESENT_INTERVAL_ONE;
}
static HRESULT WINAPI platformReset(IDirect3DDevice9* d,D3DPRESENT_PARAMETERS* p) {
 if(!p)return D3DERR_INVALIDCALL;
 if(resetReentry){reportReentry("Reset");return D3D_OK;}
 ReentryGuard guard(resetReentry);
 auto fn=(HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*))originalMethod(d,16);
 bool ours=false,output=false;{std::lock_guard<std::mutex> lock(tableMutex);ours=renderDevices.count(d)!=0;output=nativeOutputDevices.count(d)!=0;}
 if(!ours&&gameWindow&&(windowMode||fpsLimit>0)){
  D3DDEVICE_CREATION_PARAMETERS cp={};
  auto get=(HRESULT(WINAPI*)(IDirect3DDevice9*,D3DDEVICE_CREATION_PARAMETERS*))(*(void***)d)[9];
  if(get&&SUCCEEDED(get(d,&cp))&&cp.hFocusWindow==gameWindow){ours=true;std::lock_guard<std::mutex> lock(tableMutex);renderDevices.insert(d);}
 }
 UINT requestedW=p->BackBufferWidth,requestedH=p->BackBufferHeight;BOOL requestedWindowed=p->Windowed;
 UINT preservedLogicalW=0,preservedLogicalH=0;{std::lock_guard<std::mutex> lock(tableMutex);auto i=logicalRenderSizes.find(d);if(i!=logicalRenderSizes.end()){preservedLogicalW=i->second.first;preservedLogicalH=i->second.second;}}
 if(ours){releaseScaleSurface(d);releaseNativeOutputChain(d);if(output){p->Windowed=TRUE;p->FullScreen_RefreshRateInHz=0;if(legacyInternalResolutionEnabled()){p->BackBufferWidth=(UINT)internalRenderWidth;p->BackBufferHeight=(UINT)internalRenderHeight;}if(p->PresentationInterval!=D3DPRESENT_INTERVAL_IMMEDIATE)p->PresentationInterval=D3DPRESENT_INTERVAL_ONE;}else forcePresentation(*p);}HRESULT hr=fn?fn(d,p):D3DERR_INVALIDCALL;
 if(ours&&nativeResolution&&SUCCEEDED(hr)&&requestedW&&requestedH){UINT desktopW=0,desktopH=0;getNativeDisplaySize(desktopW,desktopH);bool desktopReset=requestedW==desktopW&&requestedH==desktopH&&preservedLogicalW&&preservedLogicalH&&(preservedLogicalW<desktopW||preservedLogicalH<desktopH);{std::lock_guard<std::mutex> lock(tableMutex);if(!desktopReset)logicalRenderSizes[d]={requestedW,requestedH};}if(desktopReset)logf("D3D Reset kept logical render size=%ux%u across desktop-sized reset=%ux%u",preservedLogicalW,preservedLogicalH,requestedW,requestedH);}
 if(output&&SUCCEEDED(hr))createNativeOutputChain(d);
 logf("D3D Reset request=%ux%u/%d actual=%ux%u/%d hr=%08lx",requestedW,requestedH,requestedWindowed,p->BackBufferWidth,p->BackBufferHeight,p->Windowed,hr);return hr;
}
static HRESULT WINAPI platformSetViewport(IDirect3DDevice9* d,const D3DVIEWPORT9* requested) {
 auto fn=(HRESULT(WINAPI*)(IDirect3DDevice9*,const D3DVIEWPORT9*))originalMethod(d,47);
 if(!fn||!requested)return D3DERR_INVALIDCALL;
 D3DVIEWPORT9 adjusted=*requested;bool ours=false;UINT logicalW=0,logicalH=0;
 {std::lock_guard<std::mutex> lock(tableMutex);ours=renderDevices.count(d)!=0;auto i=logicalRenderSizes.find(d);if(i!=logicalRenderSizes.end()){logicalW=i->second.first;logicalH=i->second.second;}}
 if(ours&&nativeResolution&&!scaleToFill&&adjusted.X==0&&adjusted.Y==0&&adjusted.Width==logicalW&&adjusted.Height==logicalH){
  UINT nativeW=0,nativeH=0;getNativeDisplaySize(nativeW,nativeH);adjusted.Width=nativeW;adjusted.Height=nativeH;
  bool logChange=false;{std::lock_guard<std::mutex> lock(tableMutex);auto size=std::make_pair(nativeW,nativeH);auto i=loggedNativeViewports.find(d);if(i==loggedNativeViewports.end()||i->second!=size){loggedNativeViewports[d]=size;logChange=true;}}
  if(logChange)logf("D3D SetViewport logical=%ux%u native=%ux%u",logicalW,logicalH,nativeW,nativeH);
  return fn(d,&adjusted);
 }
 return fn(d,requested);
}
static HRESULT WINAPI platformSetScissorRect(IDirect3DDevice9* d,const RECT* requested) {
 // IDirect3DDevice9::SetScissorRect is vtable slot 75. Slot 64 is GetTexture;
 // hooking it with this signature makes texture reads fail and leaves the
 // actual scissor rectangle unscaled.
 auto fn=(HRESULT(WINAPI*)(IDirect3DDevice9*,const RECT*))originalMethod(d,75);
 if(!fn||!requested)return D3DERR_INVALIDCALL;
 RECT adjusted=*requested;bool ours=false;UINT logicalW=0,logicalH=0;
 {std::lock_guard<std::mutex> lock(tableMutex);ours=renderDevices.count(d)!=0;auto i=logicalRenderSizes.find(d);if(i!=logicalRenderSizes.end()){logicalW=i->second.first;logicalH=i->second.second;}}
 if(ours&&nativeResolution&&!scaleToFill&&adjusted.left==0&&adjusted.top==0&&adjusted.right==(LONG)logicalW&&adjusted.bottom==(LONG)logicalH){
  UINT nativeW=0,nativeH=0;getNativeDisplaySize(nativeW,nativeH);adjusted.right=(LONG)nativeW;adjusted.bottom=(LONG)nativeH;
  bool logChange=false;{std::lock_guard<std::mutex> lock(tableMutex);auto size=std::make_pair(nativeW,nativeH);auto i=loggedNativeScissors.find(d);if(i==loggedNativeScissors.end()||i->second!=size){loggedNativeScissors[d]=size;logChange=true;}}
  if(logChange)logf("D3D SetScissorRect logical=%ux%u native=%ux%u",logicalW,logicalH,nativeW,nativeH);
  return fn(d,&adjusted);
 }
 return fn(d,requested);
}
static std::unordered_set<void*> loggedDepthResizes;
// Frame grab for field diagnostics: create MUSOUTweaksPC_capture.flag next to the
// game executable and the next presented backbuffer is written to
// MUSOUTweaksPC_frame.bmp. Reads the real game image even when another window
// covers it, which a desktop BitBlt cannot do.
static void captureFrameIfRequested(IDirect3DDevice9* d) {
 static ULONGLONG nextCheck=0;
 ULONGLONG now=GetTickCount64();
 if(now<nextCheck)return;
 nextCheck=now+300;
 char dir[MAX_PATH]={};GetModuleFileNameA(nullptr,dir,MAX_PATH);
 char* slash=strrchr(dir,'\\');if(!slash)return;slash[1]=0;
 char flag[MAX_PATH]={},out[MAX_PATH]={};
 strcpy_s(flag,dir);strcat_s(flag,"MUSOUTweaksPC_capture.flag");
 if(GetFileAttributesA(flag)==INVALID_FILE_ATTRIBUTES)return;
 DeleteFileA(flag);
 strcpy_s(out,dir);strcat_s(out,"MUSOUTweaksPC_frame.bmp");
 IDirect3DSurface9* back=nullptr;
 if(FAILED(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back))||!back){logf("capture: no backbuffer");return;}
 D3DSURFACE_DESC desc={};back->GetDesc(&desc);
 // A swap-chain back buffer is normally created without D3DPRESENTFLAG_LOCKABLE_BACKBUFFER,
 // so it cannot be locked directly: copy it into a system-memory surface first.
 IDirect3DSurface9* copy=nullptr;
 HRESULT hr=d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&copy,nullptr);
 if(SUCCEEDED(hr))hr=d->GetRenderTargetData(back,copy);
 D3DLOCKED_RECT locked={};
 if(SUCCEEDED(hr))hr=copy->LockRect(&locked,nullptr,D3DLOCK_READONLY);
 if(FAILED(hr)){logf("capture: copy/lock failed hr=%08lx format=%d",hr,(int)desc.Format);if(copy)copy->Release();back->Release();return;}
 DWORD width=desc.Width,height=desc.Height,rowBytes=(width*3+3)&~3U,imageBytes=rowBytes*height;
 BITMAPFILEHEADER fh={};fh.bfType=0x4D42;fh.bfOffBits=sizeof(fh)+sizeof(BITMAPINFOHEADER);fh.bfSize=fh.bfOffBits+imageBytes;
 BITMAPINFOHEADER ih={};ih.biSize=sizeof(ih);ih.biWidth=(LONG)width;ih.biHeight=(LONG)height;ih.biPlanes=1;ih.biBitCount=24;ih.biSizeImage=imageBytes;
 FILE* f=nullptr;fopen_s(&f,out,"wb");
 if(f){
  fwrite(&fh,sizeof(fh),1,f);fwrite(&ih,sizeof(ih),1,f);
  std::vector<BYTE> row(rowBytes,0);
  for(LONG y=(LONG)height-1;y>=0;y--) {
   const BYTE* src=(const BYTE*)locked.pBits+(size_t)y*locked.Pitch;
   for(DWORD x=0;x<width;x++){row[x*3]=src[x*4];row[x*3+1]=src[x*4+1];row[x*3+2]=src[x*4+2];}
   fwrite(row.data(),rowBytes,1,f);
  }
  fclose(f);logf("capture: wrote %s %ux%u",out,width,height);
 }
 copy->UnlockRect();copy->Release();back->Release();
}
static HRESULT WINAPI platformCreateDepthStencilSurface(IDirect3DDevice9* d,UINT width,UINT height,D3DFORMAT format,D3DMULTISAMPLE_TYPE multiSample,DWORD quality,BOOL discard,IDirect3DSurface9** surface,HANDLE* sharedHandle) {
 using Fn=HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,D3DFORMAT,D3DMULTISAMPLE_TYPE,DWORD,BOOL,IDirect3DSurface9**,HANDLE*);
 auto fn=(Fn)originalMethod(d,29);if(!fn)return D3DERR_INVALIDCALL;
 // The engine allocates its depth buffer from the resolution it asked for.
 // Once the backbuffer has been enlarged to the desktop size the viewport no
 // longer fits that surface, so D3D9 rejects every Clear and every depth-tested
 // draw: the frame stays stale while audio and input keep working. Grow the
 // depth surface to the backbuffer so the whole frame stays valid.
 if(nativeResolution){
  bool ours=false;{std::lock_guard<std::mutex> lock(tableMutex);ours=renderDevices.count(d)!=0;}
  if(ours){
   IDirect3DSurface9* back=nullptr;
   if(SUCCEEDED(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back))&&back){
    D3DSURFACE_DESC desc={};
    if(SUCCEEDED(back->GetDesc(&desc))&&desc.Width>width&&desc.Height>height){
     bool first=false;{std::lock_guard<std::mutex> lock(tableMutex);first=loggedDepthResizes.insert(d).second;}
     if(first)logf("D3D depth-stencil enlarged from %ux%u to backbuffer %ux%u",width,height,desc.Width,desc.Height);
     width=desc.Width;height=desc.Height;
    }
    back->Release();
   }
  }
 }
 return fn(d,width,height,format,multiSample,quality,discard,surface,sharedHandle);
}
static HRESULT WINAPI platformPresent(IDirect3DDevice9* d,const RECT* source,const RECT* destination,HWND overrideWindow,const RGNDATA* dirty) {
 if(presentReentry){reportReentry("Present");return D3D_OK;}
 ReentryGuard guard(presentReentry);
 pollCameraHotkeys();
 auto fn=(HRESULT(WINAPI*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*))originalMethod(d,17);
 bool firstParameters=false;{std::lock_guard<std::mutex> lock(tableMutex);firstParameters=loggedPrimaryPresentParameters.insert(d).second;}
 if(firstParameters)logf("D3D primary Present parameters source=%d [%ld,%ld,%ld,%ld] destination=%d [%ld,%ld,%ld,%ld] dirty=%d",source!=nullptr,source?source->left:0,source?source->top:0,source?source->right:0,source?source->bottom:0,destination!=nullptr,destination?destination->left:0,destination?destination->top:0,destination?destination->right:0,destination?destination->bottom:0,dirty!=nullptr);
 IDirect3DSwapChain9* chain=nullptr;{std::lock_guard<std::mutex> lock(tableMutex);auto i=nativeOutputChains.find(d);if(i!=nativeOutputChains.end()){chain=i->second;chain->AddRef();}}
 HRESULT copyHr=E_FAIL;bool cubicUsed=false;
 if(chain){
  IDirect3DSurface9* from=nullptr;IDirect3DSurface9* to=nullptr;
  if(SUCCEEDED(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&from))&&from&&SUCCEEDED(chain->GetBackBuffer(0,D3DBACKBUFFER_TYPE_MONO,&to))&&to)
   if(scaleFilter==2&&cubicScaleToOutput(d,from,to)){copyHr=S_OK;cubicUsed=true;}
   else copyHr=d->StretchRect(from,nullptr,to,nullptr,standardScaleFilter());
  if(from)from->Release();if(to)to->Release();
  }
 bool scaled=!chain&&scaleBackbufferToNative(d,source,destination);
 {bool ours=false;{std::lock_guard<std::mutex> lock(tableMutex);ours=renderDevices.count(d)!=0;}if(ours)captureFrameIfRequested(d);}
 HRESULT hr=fn?fn(d,scaled?nullptr:source,scaled?nullptr:destination,overrideWindow,scaled?nullptr:dirty):D3DERR_INVALIDCALL;
 // Pace after displaying this frame so the next game-loop iteration can
 // sample input and render before its next Present deadline.
 if(fpsLimit>0)pacePresentation();
 if(chain){
  if(SUCCEEDED(copyHr)&&SUCCEEDED(hr)){
   HRESULT outHr=chain->Present(nullptr,nullptr,gameWindow,nullptr,0);
   bool first=false,logFailure=false;{std::lock_guard<std::mutex> lock(tableMutex);first=loggedNativeOutputDevices.insert(d).second;if(FAILED(outHr))logFailure=loggedNativeOutputFailures.insert(d).second;}
   if(first||logFailure)logf("D3D native output present copy_hr=%08lx game_hr=%08lx output_hr=%08lx filter=%u cubic=%d",copyHr,hr,outHr,scaleFilter,cubicUsed);
  }else{
   bool logFailure=false;{std::lock_guard<std::mutex> lock(tableMutex);logFailure=loggedNativeOutputFailures.insert(d).second;}
   if(logFailure)logf("D3D native output copy skipped copy_hr=%08lx game_hr=%08lx",copyHr,hr);
  }
  chain->Release();
 }
 return hr;
}
// A repaired full-screen GS background at z=0 must not fill the depth
// buffer and hide later 3D maps/loading/results. Preserve depth testing for
// every draw; disable only depth writes on the identified background quad.
static HRESULT WINAPI orochiDrawIndexedPrimitive(IDirect3DDevice9* d,D3DPRIMITIVETYPE type,INT base,UINT min,UINT num,UINT start,UINT count) {
 if(drawReentry){reportReentry("DrawIndexedPrimitive");return D3D_OK;}
 ReentryGuard guard(drawReentry);
 auto fn=(HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT))originalMethod(d,82);if(!fn)return D3DERR_INVALIDCALL;
 DWORD fvf=0;bool background=false;
 if(num==4&&count==2&&SUCCEEDED(d->GetFVF(&fvf))&&(fvf&D3DFVF_POSITION_MASK)==D3DFVF_XYZRHW){
  IDirect3DVertexBuffer9* buffer=nullptr;UINT offset=0,stride=0;
  if(SUCCEEDED(d->GetStreamSource(0,&buffer,&offset,&stride))&&buffer){
   void* data=nullptr;const LONGLONG first=(LONGLONG)base+min;
   if(stride>=16&&stride<=256&&first>=0&&first*stride+offset<=0xffffffffu-stride*4&&SUCCEEDED(buffer->Lock(offset+(UINT)first*stride,stride*4,&data,0))){
    float left=1e9f,top=1e9f,right=-1e9f,bottom=-1e9f;bool zeroDepth=true;
    for(unsigned i=0;i<4;i++){
     const float* v=(const float*)((BYTE*)data+i*stride);
     left=std::min(left,v[0]);right=std::max(right,v[0]);top=std::min(top,v[1]);bottom=std::max(bottom,v[1]);
     if(v[2]!=0.0f)zeroDepth=false;
    }
    background=zeroDepth&&left<=0.0f&&top<=0.0f&&right>=renderWidth-1.0f&&bottom>=renderHeight-1.0f;
    buffer->Unlock();
   }
   buffer->Release();
  }
 }
 DWORD writes=FALSE;
 if(background&&SUCCEEDED(d->GetRenderState(D3DRS_ZWRITEENABLE,&writes))&&writes)d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
 HRESULT hr=fn(d,type,base,min,num,start,count);
 if(background&&writes)d->SetRenderState(D3DRS_ZWRITEENABLE,writes);
 return hr;
}
static HRESULT WINAPI platformCreateDevice(IDirect3D9* d,UINT adapter,D3DDEVTYPE type,HWND focus,DWORD flags,D3DPRESENT_PARAMETERS* p,IDirect3DDevice9** out) {
 if(!p||!out)return D3DERR_INVALIDCALL;
 if(createReentry){reportReentry("CreateDevice");return D3DERR_INVALIDCALL;}
 ReentryGuard guard(createReentry);
 attachExistingMainWindow();
 if(!gameWindow&&isSsm6DxExecutable(gameExeName)){
  HWND candidate=p->hDeviceWindow?p->hDeviceWindow:focus;
  if(candidate&&IsWindow(candidate))candidate=GetAncestor(candidate,GA_ROOT);
  DWORD owner=0;if(candidate)GetWindowThreadProcessId(candidate,&owner);
  RECT bounds={};
  if(candidate&&owner==GetCurrentProcessId()&&GetWindowRect(candidate,&bounds)){
   configureMainWindow(candidate,IsWindowUnicode(candidate),bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top);
   logf("SM6 DX main window recovered from D3D9 device hwnd=%p",candidate);
  }
 }
 logf("D3D9 CreateDevice hook entered focus=%p present_hwnd=%p game_hwnd=%p fps_limit=%d",focus,p->hDeviceWindow,gameWindow,fpsLimit);
 auto fn=(HRESULT(WINAPI*)(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9**))originalMethod(d,16);
 bool ours=gameWindow&&(focus==gameWindow||p->hDeviceWindow==gameWindow);
 bool nativeOutput=needsNativeOutputChain(*p);
 UINT requestedW=p->BackBufferWidth,requestedH=p->BackBufferHeight;BOOL requestedWindowed=p->Windowed;
 if(ours)forcePresentation(*p);HRESULT hr=fn?fn(d,adapter,type,focus,flags,p,out):D3DERR_INVALIDCALL;
 logf("D3D CreateDevice device=%p focus=%p present_hwnd=%p game_hwnd=%p ours=%d flags=%08lx request=%ux%u/%d actual=%ux%u/%d hr=%08lx",SUCCEEDED(hr)&&out?*out:nullptr,focus,p->hDeviceWindow,gameWindow,ours,flags,requestedW,requestedH,requestedWindowed,p->BackBufferWidth,p->BackBufferHeight,p->Windowed,hr);
 if(SUCCEEDED(hr)&&*out){
  if(ours&&(nativeResolution||fpsLimit>0||cameraTweakActive||nativeOutput||frameCapture)){hookMethod(*out,17,(void*)platformPresent);logf("D3D9 Present hook installed fps_limit=%d single_present=%d secondary_chain=%d capture=%d",fpsLimit,singlePresentMode,nativeOutput,frameCapture);}
  if(ours){
   UINT logicalW=requestedW,logicalH=requestedH;
   if(((legacyInternalResolutionEnabled()&&nativeOutput)||nativeResolution)&&(!logicalW||!logicalH))getNativeDisplaySize(logicalW,logicalH);
   {std::lock_guard<std::mutex> lock(tableMutex);renderDevices.insert(*out);if(nativeResolution||(legacyInternalResolutionEnabled()&&nativeOutput))logicalRenderSizes[*out]={logicalW,logicalH};if(nativeOutput)nativeOutputDevices.insert(*out);}
   hookMethod(*out,16,(void*)platformReset);
   // The engine sizes its depth buffer from the resolution it asked for. Once the
   // backbuffer is enlarged that surface no longer covers the viewport, so D3D9
   // rejects every Clear and every depth-tested draw and the picture freezes.
   if(nativeResolution)hookMethod(*out,29,(void*)platformCreateDepthStencilSurface);
   if(!_stricmp(gameExeName,"OROCHI_Z_TC.exe")&&internalRenderHeight>2048){
    hookMethod(*out,82,(void*)orochiDrawIndexedPrimitive);
    logf("Orochi high-resolution background-only depth-write correction installed");
   }if((nativeResolution&&!scaleToFill)||(legacyInternalResolutionEnabled()&&nativeOutput)){hookMethod(*out,47,(void*)platformSetViewport);hookMethod(*out,75,(void*)platformSetScissorRect);}
   IDirect3DSurface9* back=nullptr;
   if(SUCCEEDED((*out)->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back))){D3DSURFACE_DESC desc={};back->GetDesc(&desc);logf("actual backbuffer %ux%u",desc.Width,desc.Height);back->Release();}
   if(nativeOutput)createNativeOutputChain(*out);
  }
 }
 return hr;
}
static IDirect3D9* WINAPI platformCreateD3D(UINT sdk) {
 initializeAll();patchSsm7Resolution();patchSsm7Camera();patchOrochiCamera();logf("Direct3DCreate9 hook entered fps_limit=%d borderless=%d",fpsLimit,windowMode);
 IDirect3D9* d=realCreateD3D?realCreateD3D(sdk):nullptr;
 if(d&&(windowMode||fpsLimit>0||cameraTweakActive))hookMethod(d,16,(void*)platformCreateDevice);return d;
}
static bool d3d9DeviceTable(void** table,uintptr_t moduleBase,uintptr_t moduleEnd,void* present,void* reset) {
 void* methods[119]={};SIZE_T bytesRead=0;
 if(!table||!ReadProcessMemory(GetCurrentProcess(),table,methods,sizeof(methods),&bytesRead)||bytesRead!=sizeof(methods))return false;
 if(methods[16]!=reset||methods[17]!=present)return false;
 unsigned inImage=0;
 for(size_t i=0;i<119;i++){uintptr_t fn=(uintptr_t)methods[i];if(fn>=moduleBase&&fn<moduleEnd)inImage++;}
 return inImage>=100;
}
static unsigned hookExistingD3D9Tables(void* present,void* reset,void** probeTable) {
 // The early Direct3DCreate9/CreateDevice hook normally owns the game device a
 // few milliseconds after startup. Never install a second hook over a table we
 // already control: with an overlay such as Steam's gameoverlayrenderer in the
 // chain that produced a wrapper loop and ended in a stack overflow.
 {std::lock_guard<std::mutex> lock(tableMutex);if(!renderDevices.empty())return 0;}
 HMODULE module=GetModuleHandleA("d3d9.dll");if(!module)return 0;
 BYTE* base=(BYTE*)module;auto dos=(IMAGE_DOS_HEADER*)base;if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return 0;
 auto nt=(IMAGE_NT_HEADERS*)(base+dos->e_lfanew);uintptr_t moduleBase=(uintptr_t)base,moduleEnd=moduleBase+nt->OptionalHeader.SizeOfImage;
 SYSTEM_INFO info={};GetSystemInfo(&info);uintptr_t address=(uintptr_t)info.lpMinimumApplicationAddress,maximum=(uintptr_t)info.lpMaximumApplicationAddress;
 unsigned hooked=0;void* firstTable=nullptr;BYTE scanBuffer[64*1024];
 while(address<maximum){
  MEMORY_BASIC_INFORMATION region={};SIZE_T queried=VirtualQuery((void*)address,&region,sizeof(region));if(!queried)break;
  uintptr_t start=(uintptr_t)region.BaseAddress,end=start+region.RegionSize;
  DWORD protection=region.Protect&0xff;
  bool readable=protection==PAGE_READONLY||protection==PAGE_READWRITE||protection==PAGE_WRITECOPY||protection==PAGE_EXECUTE_READ||protection==PAGE_EXECUTE_READWRITE||protection==PAGE_EXECUTE_WRITECOPY;
  if(region.State==MEM_COMMIT&&region.Type==MEM_PRIVATE&&readable&&!(region.Protect&PAGE_GUARD)){
   uintptr_t cursor=(start+sizeof(void*)-1)&~(uintptr_t)(sizeof(void*)-1);
   while(cursor+sizeof(void*)<=end){
    SIZE_T chunkSize=(SIZE_T)(end-cursor);if(chunkSize>sizeof(scanBuffer))chunkSize=sizeof(scanBuffer);
    chunkSize&=~(SIZE_T)(sizeof(void*)-1);if(!chunkSize)break;
    SIZE_T bytesRead=0;
    // VirtualQuery is only a snapshot; another thread can free or protect a
    // heap region before the scan reaches it. Copy safely instead of reading
    // the live address directly, so an ordinary allocation race cannot crash
    // the game from this background D3D9 hook-discovery thread.
    if(ReadProcessMemory(GetCurrentProcess(),(void*)cursor,scanBuffer,chunkSize,&bytesRead)&&bytesRead==chunkSize){
     for(SIZE_T offset=0;offset+sizeof(void*)<=chunkSize;offset+=sizeof(void*)){
      void* candidate=nullptr;memcpy(&candidate,scanBuffer+offset,sizeof(candidate));if(candidate!=present)continue;
      uintptr_t word=cursor+offset;if(word<start+17*sizeof(void*))continue;
      void** table=(void**)(word-17*sizeof(void*));
      if((uintptr_t)table+119*sizeof(void*)>end)continue;
      if(table==probeTable||!d3d9DeviceTable(table,moduleBase,moduleEnd,present,reset))continue;
      hookTable(table,17,(void*)platformPresent);hookTable(table,16,(void*)platformReset);
      if(!firstTable)firstTable=table;hooked++;
     }
   }
    cursor+=chunkSize;
   }
  }
  if(end<=address)break;address=end;
 }
 if(hooked)logf("late D3D9 game vtables hooked=%u first=%p fps_limit=%d",hooked,firstTable,fpsLimit);
 return hooked;
}
// DirectInput8 may initialize after the game has created its D3D9 device.
// A probe supplies this runtime's Present/Reset addresses; scan private heap
// vtables for the matching device table, then hook the game's existing one.
static DWORD WINAPI lateD3DPresentWatch(void*) {
 SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
 HMODULE module=GetModuleHandleA("d3d9.dll");if(!module)module=LoadLibraryA("d3d9.dll");
 auto create=module?(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9"):nullptr;
 if(!create){logf("late D3D9 hook failed: Direct3DCreate9 unavailable");return 0;}
 IDirect3D9* api=create(D3D_SDK_VERSION);
 if(!api){logf("late D3D9 hook failed: Direct3DCreate9 returned null");return 0;}
 HWND probe=CreateWindowExA(0,"STATIC","MUSOUTweaksPC D3D probe",WS_POPUP,0,0,16,16,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);
 if(!probe){api->Release();logf("late D3D9 hook failed: probe window unavailable");return 0;}
 D3DPRESENT_PARAMETERS p={};p.BackBufferWidth=16;p.BackBufferHeight=16;p.BackBufferFormat=D3DFMT_UNKNOWN;
 p.BackBufferCount=1;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=probe;p.Windowed=TRUE;p.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
 IDirect3DDevice9* device=nullptr;HRESULT hr=api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,probe,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&device);
 if(FAILED(hr)||!device){
  p={};p.BackBufferWidth=16;p.BackBufferHeight=16;p.BackBufferFormat=D3DFMT_UNKNOWN;p.BackBufferCount=1;
  p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=probe;p.Windowed=TRUE;p.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
  hr=api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,probe,D3DCREATE_HARDWARE_VERTEXPROCESSING,&p,&device);
 }
 if(FAILED(hr)||!device){DestroyWindow(probe);api->Release();logf("late D3D9 hook failed: probe CreateDevice hr=%08lx",hr);return 0;}
 void** probeTable=*(void***)device;void* present=probeTable[17];void* reset=probeTable[16];
 logf("late D3D9 watcher started present=%p reset=%p",present,reset);
 if(GetModuleHandleA("gameoverlayrenderer.dll")){
  logf("late D3D9 watcher skipped: Steam overlay is loaded and the early CreateDevice hook owns the game device");
  device->Release();DestroyWindow(probe);api->Release();return 0;
 }
 for(unsigned attempt=1;attempt<=120;attempt++){
  // Give the game time to create its first device through the early hook so the
  // ownership check below sees it; a late-loading dinput8 simply keeps waiting.
  if(attempt==1)Sleep(400);
  bool earlyHooked=false;{std::lock_guard<std::mutex> lock(tableMutex);earlyHooked=!renderDevices.empty();}
  if(earlyHooked){
   logf("late D3D9 watcher stopped after %u scans: early CreateDevice hook already hooked the game device",attempt-1);
   device->Release();DestroyWindow(probe);api->Release();return 0;
  }
  unsigned hooked=hookExistingD3D9Tables(present,reset,probeTable);
  if(hooked){logf("late D3D9 watcher attached after %u scans",attempt);device->Release();DestroyWindow(probe);api->Release();return 0;}
  if(attempt%20==0)logf("late D3D9 watcher waiting for game device attempt=%u",attempt);
  Sleep(500);
 }
 logf("late D3D9 hook failed: no matching live game device after 30 seconds");
 device->Release();DestroyWindow(probe);api->Release();return 0;
}
static bool attachLateD3DPresentHook() {
 if(lateD3DHookAttempted||(!windowMode&&fpsLimit<=0))return false;
 // DW3/DW4's Direct3DCreate9 entry point is imported and hooked before the
 // games create their devices. The extra probe/heap scan is redundant here;
 // it had started immediately before the recorded DW4 startup stack overflow.
 if(isLegacyHighResolutionExecutable(gameExeName)){
  lateD3DHookAttempted=true;logf("late D3D9 watcher skipped for %s: early CreateDevice hook is active",gameExeName);return false;
 }
 lateD3DHookAttempted=true;
 HANDLE thread=CreateThread(nullptr,0,lateD3DPresentWatch,nullptr,0,nullptr);
 if(!thread){lateD3DHookAttempted=false;logf("late D3D9 watcher failed to start error=%lu",GetLastError());return false;}
 CloseHandle(thread);return true;
}
static bool replaceCode(BYTE* location,const BYTE* expected,SIZE_T n,const BYTE* replacement,SIZE_T size) {
 if(memcmp(location,expected,n))return false;DWORD old=0;
 if(!VirtualProtect(location,size,PAGE_EXECUTE_READWRITE,&old))return false;
 memcpy(location,replacement,size);FlushInstructionCache(GetCurrentProcess(),location,size);
 DWORD discard;VirtualProtect(location,size,old,&discard);return true;
}
static bool constantGetter(BYTE* base,DWORD rva,int value) {
 const BYTE prefix[]={0x0f,0xb6,0x05};BYTE code[]={0xb8,0,0,0,0,0xc3};memcpy(code+1,&value,4);
 return replaceCode(base+rva,prefix,sizeof(prefix),code,sizeof(code));
}
// Port of the SM6 resolution mod's engine-constant method. These RVAs are
// verified against the unpacked DW8 Launch.exe build, never against SM6 offsets.
// Preserve the lens and UI projection: move only the 3D viewpoint along
// its eye-to-target line. Read the setting periodically so it can be tuned
// in combat without restarting. Caller checks exclude unrelated math users.
struct Ssm7CameraVector {float x,y,z;};
using Ssm7LookAtFn=D3DMATRIX*(WINAPI*)(D3DMATRIX*,const Ssm7CameraVector*,const Ssm7CameraVector*,const Ssm7CameraVector*);
static Ssm7LookAtFn realSsm7LookAt=nullptr;
static volatile LONG cameraDistancePercent=100,cameraPollBusy=0;
static DWORD cameraSettingsTick=0;
static int cameraHotkeys=1,cameraStep=5,cameraModifiers=3;
static int cameraFarKey=VK_PRIOR,cameraNearKey=VK_NEXT,cameraResetKey=VK_HOME;
static void readCameraSettings() {
 auto opt=[](const char* name,int fallback){return (int)GetPrivateProfileIntA("platform",name,fallback,iniPath);};
 InterlockedExchange(&cameraDistancePercent,std::clamp(opt("camera_distance_percent",100),50,300));
 cameraHotkeys=opt("camera_hotkeys",1)!=0;
 cameraStep=std::clamp(opt("camera_distance_step",5),1,50);
 cameraModifiers=opt("camera_hotkey_modifiers",3)&7;
 cameraFarKey=std::clamp(opt("camera_hotkey_farther",VK_PRIOR),0,255);
 cameraNearKey=std::clamp(opt("camera_hotkey_nearer",VK_NEXT),0,255);
 cameraResetKey=std::clamp(opt("camera_hotkey_reset",VK_HOME),0,255);
 cameraSettingsTick=GetTickCount();
}
static void pollCameraHotkeys() {
 if(!cameraTweakActive||InterlockedCompareExchange(&cameraPollBusy,1,0))return;
 struct Unlock {~Unlock(){InterlockedExchange(&cameraPollBusy,0);}} unlock;
 DWORD now=GetTickCount();
 if((DWORD)(now-cameraSettingsTick)>=500)readCameraSettings();
 static unsigned previous=0;
 static DWORD repeatAt=0;
 static bool armed=false;
 DWORD owner=0;GetWindowThreadProcessId(GetForegroundWindow(),&owner);
 if(!cameraHotkeys||owner!=GetCurrentProcessId()){previous=0;armed=false;repeatAt=0;return;}
 auto down=[](int vk){return vk>0&&(GetAsyncKeyState(vk)&0x8000)!=0;};
 bool modifiers=(!(cameraModifiers&1)||down(VK_CONTROL))&&(!(cameraModifiers&2)||down(VK_MENU))&&(!(cameraModifiers&4)||down(VK_SHIFT));
 unsigned held=modifiers?((down(cameraFarKey)?1u:0u)|(down(cameraNearKey)?2u:0u)|(down(cameraResetKey)?4u:0u)):0;
 // A combination held while switching into the game must be released first.
 if(!armed){if(!held)armed=true;previous=held;return;}
 unsigned pressed=held&~previous;
 int old=(int)InterlockedCompareExchange(&cameraDistancePercent,0,0),value=old;
 if(pressed&4)value=100;
 else if((held&3)!=3&&!(held&4)){
  unsigned direction=held&3;
  if((pressed&3)||(direction&&repeatAt&&(LONG)(now-repeatAt)>=0)){
   if(direction==1)value+=cameraStep;
   else if(direction==2)value-=cameraStep;
   repeatAt=now+((pressed&3)?350u:150u);
  }
 }
 if(!held)repeatAt=0;
 previous=held;value=std::clamp(value,50,300);
 if(value!=old){
  InterlockedExchange(&cameraDistancePercent,value);
  char number[12];sprintf_s(number,"%d",value);
  BOOL saved=WritePrivateProfileStringA("platform","camera_distance_percent",number,iniPath);
  logf("camera hotkey distance=%d saved=%d",value,saved);
 }
}

static D3DMATRIX* WINAPI ssm7CameraLookAt(D3DMATRIX* matrix,const Ssm7CameraVector* eye,const Ssm7CameraVector* target,const Ssm7CameraVector* up) {
 if(!realSsm7LookAt)return nullptr;
 const uintptr_t caller=(uintptr_t)_ReturnAddress()-(uintptr_t)GetModuleHandleA(nullptr);
 if(caller!=0x11585a&&caller!=0x22c12b&&caller!=0x22c8b0&&caller!=0x22eb4f)return realSsm7LookAt(matrix,eye,target,up);
 const int percent=(int)InterlockedCompareExchange(&cameraDistancePercent,0,0);
 if(percent==100||!eye||!target)return realSsm7LookAt(matrix,eye,target,up);
 const float scale=percent/100.0f;
 const Ssm7CameraVector adjusted={target->x+(eye->x-target->x)*scale,target->y+(eye->y-target->y)*scale,target->z+(eye->z-target->z)*scale};
 return realSsm7LookAt(matrix,&adjusted,target,up);
}
static void patchSsm7Camera() {
 if(cameraTweakActive||_stricmp(gameExeName,"Launch.exe"))return;
 BYTE* base=(BYTE*)GetModuleHandleA(nullptr);
 auto nt=(IMAGE_NT_HEADERS*)(base+((IMAGE_DOS_HEADER*)base)->e_lfanew);
 if(nt->OptionalHeader.SizeOfImage!=0x9ac000)return;
 const BYTE indirectCall[]={0xff,0x15};
 if(memcmp(base+0x115854,indirectCall,2)||*(DWORD*)(base+0x115856)!=(DWORD)(uintptr_t)(base+0x5d32f0))return;
 void** slot=(void**)(base+0x5d32f0);
 DWORD old=0;if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old))return;
 realSsm7LookAt=(Ssm7LookAtFn)*slot;
 InterlockedExchangePointer(slot,(void*)ssm7CameraLookAt);
 DWORD discard;VirtualProtect(slot,sizeof(void*),old,&discard);
 readCameraSettings();
 cameraTweakActive=true;
 logf("SSM7 3D camera distance installed; camera_distance_percent=50..300, live INI reload, 100=original");
}
// Orochi has a CPU projection for labels and a separate PC renderer camera.
// Adjust both from the same temporary eye position, then restore the native
// pose so smoothing never compounds the zoom. Slots 0/1 are the player views;
// the preparation map uses a different camera and must keep its own framing.
using OrochiProjectionFn=uintptr_t(__cdecl*)(BYTE*);
using OrochiRendererCameraFn=uintptr_t(__cdecl*)(BYTE*,BYTE*);
static OrochiProjectionFn originalOrochiProjection=nullptr;
static OrochiRendererCameraFn originalOrochiRendererCamera=nullptr;
static bool adjustOrochiCameraEye(BYTE* camera,float* saved) {
 if(!camera)return false;
 BYTE* base=(BYTE*)GetModuleHandleA(nullptr);
 BYTE** views=(BYTE**)(base+0x38a3d0);
 if(camera!=views[0]&&camera!=views[1])return false;
 int percent=(int)InterlockedCompareExchange(&cameraDistancePercent,0,0);
 if(percent==100)return false;
 float* eye=(float*)(camera+0x1c0);const float* target=(float*)(camera+0x1d0);
 memcpy(saved,eye,3*sizeof(float));float scale=percent/100.0f;
 for(unsigned i=0;i<3;i++)eye[i]=target[i]+(eye[i]-target[i])*scale;
 return true;
}
static uintptr_t __cdecl orochiCameraProjection(BYTE* camera) {
 float saved[3];bool changed=adjustOrochiCameraEye(camera,saved);
 uintptr_t result=originalOrochiProjection(camera);
 if(changed)memcpy(camera+0x1c0,saved,sizeof(saved));
 return result;
}
static uintptr_t __cdecl orochiRendererCamera(BYTE* renderer,BYTE* camera) {
 float saved[3];bool changed=adjustOrochiCameraEye(camera,saved);
 uintptr_t result=originalOrochiRendererCamera(renderer,camera);
 if(changed)memcpy(camera+0x1c0,saved,sizeof(saved));
 return result;
}
static bool buildOrochiCameraTrampoline(BYTE* site,const BYTE* expected,SIZE_T size,void* hook,void** original) {
 if(memcmp(site,expected,size))return false;
 BYTE* code=(BYTE*)VirtualAlloc(nullptr,size+5,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
 if(!code)return false;
 // These verified prologues contain only stack/register instructions.
 memcpy(code,expected,size);code[size]=0xe9;
 LONG back=(LONG)((site+size)-(code+size+5));memcpy(code+size+1,&back,4);
 BYTE jump[8]={0xe9,0,0,0,0,0x90,0x90,0x90};LONG forward=(LONG)((BYTE*)hook-(site+5));memcpy(jump+1,&forward,4);
 *original=code;FlushInstructionCache(GetCurrentProcess(),code,size+5);
 if(!replaceCode(site,expected,size,jump,size)){VirtualFree(code,0,MEM_RELEASE);*original=nullptr;return false;}
 DWORD old;VirtualProtect(code,size+5,PAGE_EXECUTE_READ,&old);return true;
}
static void patchOrochiCamera() {
 if(cameraTweakActive||_stricmp(gameExeName,"OROCHI_Z_TC.exe")||!nativeMenuMap)return;
 BYTE* base=(BYTE*)GetModuleHandleA(nullptr);
 const BYTE projection[]={0x83,0xec,0x74,0x53,0x8b,0x5c,0x24,0x7c};
 const BYTE renderer[]={0x83,0xec,0x30,0x56,0x8b,0x74,0x24,0x3c};
 if(memcmp(base+0xf9c70,projection,sizeof(projection))||memcmp(base+0xf9950,renderer,sizeof(renderer)))return;
 if(!buildOrochiCameraTrampoline(base+0xf9c70,projection,sizeof(projection),(void*)orochiCameraProjection,(void**)&originalOrochiProjection))return;
 if(!buildOrochiCameraTrampoline(base+0xf9950,renderer,sizeof(renderer),(void*)orochiRendererCamera,(void**)&originalOrochiRendererCamera))return;
 readCameraSettings();cameraTweakActive=true;
 logf("Orochi player camera distance installed: CPU projection + renderer; preparation map excluded");
}
static void patchSsm7Resolution() {
 // Port the config-driven width/height replacement model from SM6 DX's
 // resolution_for_SM6TC mod. Its addresses belong to SM6TC's packed image;
 // Launch.exe uses this separately verified table instead.
 if(ssm7ResolutionPatched||_stricmp(gameExeName,"Launch.exe")||internalRenderWidth<640||internalRenderHeight<360)return;
 if((ULONGLONG)internalRenderWidth*9!=(ULONGLONG)internalRenderHeight*16){logf("SSM7 engine resolution rejected: expected 16:9");return;}
 BYTE* base=(BYTE*)GetModuleHandleA(nullptr);
 struct Site {DWORD rva;BYTE opcode;DWORD original;bool width;BYTE stackOffset=0;};
 static const Site sites[]={
  {0x30a0,0xba,480,false},
  {0x30a7,0xba,600,false},
  {0x30ae,0xba,720,false},
  {0x30b5,0xba,960,false},
  {0x30bc,0xba,1024,false},
  {0x30c3,0xba,900,false},
  {0x30ca,0xba,1200,false},
  {0x30d1,0xba,1080,false},
  {0x30d8,0xba,768,false},
  {0x30e9,0xb8,640,true},
  {0x30f3,0xb8,800,true},
  {0x30fd,0xb8,1280,true},
  {0x3107,0xb8,1600,true},
  {0x3111,0xb8,1920,true},
  {0x311b,0xb8,1024,true},
  {0x31b7,0xb8,480,false},
  {0x31bd,0xb8,600,false},
  {0x31c3,0xb8,720,false},
  {0x31c9,0xb8,960,false},
  {0x31cf,0xb8,1024,false},
  {0x31d5,0xb8,900,false},
  {0x31db,0xb8,1200,false},
  {0x31e1,0xb8,1080,false},
  {0x31e7,0xb8,768,false},
  {0x3767,0xb8,640,true},
  {0x376d,0xb8,800,true},
  {0x3773,0xb8,1280,true},
  {0x3779,0xb8,1600,true},
  {0x377f,0xb8,1920,true},
  {0x3785,0xb8,1024,true},
  {0x5b755d,0xb8,480,false},
  {0x5b7563,0xb8,600,false},
  {0x5b7569,0xb8,720,false},
  {0x5b756f,0xb8,960,false},
  {0x5b7575,0xb8,1024,false},
  {0x5b757b,0xb8,900,false},
  {0x5b7581,0xb8,1200,false},
  {0x5b7587,0xb8,1080,false},
  {0x5b758d,0xb8,768,false},
  {0x5b75cd,0xb8,640,true},
  {0x5b75d3,0xb8,800,true},
  {0x5b75d9,0xb8,1280,true},
  {0x5b75df,0xb8,1600,true},
  {0x5b75e5,0xb8,1920,true},
  {0x5b75eb,0xb8,1024,true},
  {0x5c2c52,0xbe,640,true},
  {0x5c2c59,0xbe,800,true},
  {0x5c2c60,0xbe,1280,true},
  {0x5c2c67,0xbe,1600,true},
  {0x5c2c6e,0xbe,1920,true},
  {0x5c2c75,0xbe,1024,true},
  {0x5ca198,0xb8,640,true},
  {0x5ca19f,0xb8,800,true},
  {0x5ca1a6,0xb8,1280,true},
  {0x5ca1ad,0xb8,1600,true},
  {0x5ca1b4,0xb8,1920,true},
  {0x5ca1bb,0xb8,1024,true},
  {0x5ca276,0xb8,640,true},
  {0x5ca27d,0xb8,800,true},
  {0x5ca284,0xb8,1280,true},
  {0x5ca28b,0xb8,1600,true},
  {0x5ca292,0xb8,1920,true},
  {0x5ca299,0xb8,1024,true},
  {0x5caaac,0xb8,640,true},
  {0x5caab3,0xb8,800,true},
  {0x5caaba,0xb8,1280,true},
  {0x5caac1,0xb8,1600,true},
  {0x5caac8,0xb8,1920,true},
  {0x5caacf,0xb8,1024,true},
  {0x5cabc8,0xb8,640,true},
  {0x5cabcf,0xb8,800,true},
  {0x5cabd6,0xb8,1280,true},
  {0x5cabdd,0xb8,1600,true},
  {0x5cabe4,0xb8,1920,true},
  {0x5cabeb,0xb8,1024,true},
  {0x5cac99,0xb8,640,true},
  {0x5caca0,0xb8,800,true},
  {0x5caca7,0xb8,1280,true},
  {0x5cacae,0xb8,1600,true},
  {0x5cacb5,0xb8,1920,true},
  {0x5cacbc,0xb8,1024,true},
  {0x5caf3b,0xbd,640,true},
  {0x5caf42,0xbd,800,true},
  {0x5caf49,0xbd,1280,true},
  {0x5caf50,0xbd,1600,true},
  {0x5caf57,0xbd,1920,true},
  {0x5caf5e,0xbd,1024,true},
  {0x5caf75,0xb8,640,true},
  {0x5caf7c,0xb8,800,true},
  {0x5caf83,0xb8,1280,true},
  {0x5caf8a,0xb8,1600,true},
  {0x5caf91,0xb8,1920,true},
  {0x5caf98,0xb8,1024,true},
  // Battle camera and HUD clipping inline width profiles use stack stores.
  // Their height comes from the shared getter; leaving these at 800 while
  // replacing height with 2160 makes a narrow viewport and crops the HUD.
  {0x581979,0xc7,640,true,0x0c},
  {0x581983,0xc7,800,true,0x0c},
  {0x58198d,0xc7,1280,true,0x0c},
  {0x581997,0xc7,1600,true,0x0c},
  {0x5819a1,0xc7,1920,true,0x0c},
  {0x5819ab,0xc7,1024,true,0x0c},
  {0x5bfc28,0xc7,640,true,0x10},
  {0x5bfc32,0xc7,800,true,0x10},
  {0x5bfc3c,0xc7,1280,true,0x10},
  {0x5bfc46,0xc7,1600,true,0x10},
  {0x5bfc50,0xc7,1920,true,0x10},
  {0x5bfc5a,0xc7,1024,true,0x10},
  {0x5bdad9,0xb8,360,false},
  {0x5bdae3,0xb8,450,false},
  {0x5bdaed,0xb8,720,false},
  {0x5bdaf7,0xb8,900,false},
  {0x5bdb01,0xb8,1080,false},
 };
 auto nt=(IMAGE_NT_HEADERS*)(base+((IMAGE_DOS_HEADER*)base)->e_lfanew);
 if(nt->OptionalHeader.SizeOfImage!=0x9ac000)return;
 // Validate every instruction before modifying any; retry after Steam unpacking.
 for(const auto& site:sites){
  if(memcmp(base+site.rva,&site.original,4))return;
  if(site.stackOffset){
   const BYTE store[]={0xc7,0x44,0x24,site.stackOffset};
   if(memcmp(base+site.rva-4,store,sizeof(store)))return;
  }else if(base[site.rva-1]!=site.opcode)return;
 }
 DWORD old=0;const SIZE_T span=0x5d2000;
 if(!VirtualProtect(base+0x1000,span,PAGE_EXECUTE_READWRITE,&old)){logf("SSM7 resolution VirtualProtect failed=%lu",GetLastError());return;}
 for(const auto& site:sites){DWORD value=site.width?internalRenderWidth:internalRenderHeight;memcpy(base+site.rva,&value,4);}
 FlushInstructionCache(GetCurrentProcess(),base+0x1000,span);DWORD discard;VirtualProtect(base+0x1000,span,old,&discard);
 ssm7ResolutionPatched=true;
 logf("SSM7 engine resolution patched %dx%d sites=%u (engine resolution profiles replaced)",internalRenderWidth,internalRenderHeight,(unsigned)(sizeof(sites)/sizeof(sites[0])));
}
static void platformConfigure() {
 auto opt=[](const char* key,int def){return (int)GetPrivateProfileIntA("platform",key,def,iniPath);};
 sharedInput=opt("shared_input",1);windowMode=std::clamp(opt("window_mode",1),0,1);
 imeIsolation=opt("disable_game_ime",1);comCompatibility=opt("com_apartment_compat",1);quickClose=opt("close_without_confirmation",1);
 fpsLimit=std::clamp(opt("fps_limit",0),0,240);
 if(windowMode){
  HMODULE user=GetModuleHandleA("user32.dll");auto dpi=(BOOL(WINAPI*)(HANDLE))GetProcAddress(user,"SetProcessDpiAwarenessContext");
  auto threadDpi=(HANDLE(WINAPI*)(HANDLE))GetProcAddress(user,"SetThreadDpiAwarenessContext");
  BOOL processOk=dpi?dpi((HANDLE)-4):FALSE;HANDLE oldContext=threadDpi?threadDpi((HANDLE)-4):nullptr;
  logf("DPI awareness process=%d thread_previous=%p",processOk,oldContext);
 }
 MONITORINFO monitor={sizeof(monitor)};GetMonitorInfoA(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&monitor);monitorBounds=monitor.rcMonitor;
 renderWidth=std::clamp(opt("render_width",0),0,8192);renderHeight=std::clamp(opt("render_height",0),0,8192);
 nativeResolution=std::clamp(opt("native_resolution",0),0,1)!=0;
 scaleToFill=std::clamp(opt("scale_to_fill",0),0,1)!=0;
 scaleAspectMode=std::clamp(opt("scale_aspect_mode",0),0,1);
 scaleFilter=std::clamp(opt("scale_filter",2),0,2);
 singlePresentMode=std::clamp(opt("single_present",1),0,1)!=0;
 frameCapture=opt("frame_capture",0)!=0;
 internalRenderWidth=std::clamp(opt("internal_render_width",0),0,8192);
 internalRenderHeight=std::clamp(opt("internal_render_height",0),0,8192);
 forceNativeBuffer=(renderWidth!=0||renderHeight!=0);
 if(!renderWidth)renderWidth=monitorBounds.right-monitorBounds.left;if(!renderHeight)renderHeight=monitorBounds.bottom-monitorBounds.top;
 renderWidth=std::max(renderWidth,640);renderHeight=std::max(renderHeight,360);
 char exe[MAX_PATH]={};GetModuleFileNameA(nullptr,exe,MAX_PATH);const char* name=strrchr(exe,'\\');name=name?name+1:exe;
 strcpy_s(gameExeName,name);
 if(isLegacyHighResolutionExecutable(name)&&(internalRenderWidth||internalRenderHeight||forceNativeBuffer||nativeResolution||scaleToFill)){
  // DW4 Hyper / DW5 Special keep their own 1280x960 render target; only the
  // borderless window and the controller/IME fixes apply. Their engine keeps
  // UI and scene coordinates in the selected mode, so a larger backbuffer
  // would crop or black out the picture.
  logf("legacy resolution override refused for %s: D3D-only sizing would crop the scene; use the original game resolution",name);
  internalRenderWidth=internalRenderHeight=0;forceNativeBuffer=false;nativeResolution=false;scaleToFill=false;
 }
#ifndef PADFIX_PLATFORM_ONLY
 if(!_stricmp(name,"SW2.exe")){
  sw2ResolutionInitialize();
  logf("SW2 game-specific engine resolution/HUD worker started; generic D3D buffer override is disabled");
 }
#endif
 if(!_stricmp(name,"OROCHI_Z_TC.exe")){
  forceNativeBuffer=true;
  if(windowMode&&internalRenderWidth>=640&&internalRenderHeight>=360){
   renderWidth=internalRenderWidth;renderHeight=internalRenderHeight;
   nativeResolution=false;scaleToFill=false;
   logf("Orochi Z independent engine render=%dx%d; primary swapchain desktop presentation",renderWidth,renderHeight);
  }
 }
 if(imeIsolation){HMODULE imm=systemDll("imm32.dll");if(imm){disableIme=(decltype(disableIme))GetProcAddress(imm,"ImmDisableIME");associateIme=(decltype(associateIme))GetProcAddress(imm,"ImmAssociateContextEx");}
  // Some older Koei titles crash during pre-window startup while third-party
  // IME hooks are active. Disable IME at platform initialization as well as
  // immediately before main-window creation.
  if(disableIme)logf("ImmDisableIME during platform init result=%d",disableIme((DWORD)-1));
 }
 logf("platform hooks=%d mask=%08lx shared=%d borderless=%d render=%dx%d native_render=%d scale_to_fill=%d scale_aspect_mode=%d scale_filter=%u single_present=%d IME=%d COM=%d close=%d fps_limit=%d",earlyHookCount,earlyHookMask,sharedInput,windowMode,renderWidth,renderHeight,nativeResolution,scaleToFill,scaleAspectMode,scaleFilter,singlePresentMode,imeIsolation,comCompatibility,quickClose,fpsLimit);
 attachExistingMainWindow();
 startMainWindowWatch();
 if(windowMode||fpsLimit>0)attachLateD3DPresentHook();
}
// The inherited GS sprite format stores Y in unsigned 12.4 fixed point
// with a 2048-pixel bias. A 2160-pixel bottom edge wraps to -1936.
static void repairOrochiSpriteY(BYTE* sprite,unsigned kind) {
 if(!sprite||internalRenderHeight<=2048)return;
 unsigned offsets[4]={0x858,0x860,0x868,0x870};unsigned count=4;
 if(kind==1){offsets[0]=0x858;offsets[1]=0x870;count=2;}
 if(kind==2){offsets[0]=0x8a8;offsets[1]=0x8b0;offsets[2]=0x8b8;count=3;}
 bool onScreen=false;for(unsigned i=0;i<count;i++){int y=*(int*)(sprite+offsets[i]);if(y>=0&&y<=internalRenderHeight)onScreen=true;}
 if(!onScreen)return;
 for(unsigned i=0;i<count;i++){int& y=*(int*)(sprite+offsets[i]);if(y<-1024&&y+4096>=0&&y+4096<=internalRenderHeight)y+=4096;}
}
static void __cdecl repairOrochiQuad(BYTE* s){repairOrochiSpriteY(s,0);}
static void __cdecl repairOrochiRect(BYTE* s){repairOrochiSpriteY(s,1);}
static void __cdecl repairOrochiTriangle(BYTE* s){repairOrochiSpriteY(s,2);}
static bool installOrochiSpriteFix(BYTE* base) {
 struct Site {DWORD rva;SIZE_T size;BYTE bytes[7];};
 const Site sites[]={{0x1cd830,7,{0x56,0x8b,0x35,0x94,0x96,0x40,0x01}},{0x1cd8c0,5,{0x83,0xec,0x08,0x53,0x56}},{0x1cd630,5,{0x83,0xec,0x0c,0x53,0x55}}};
 for(const auto& site:sites)if(memcmp(base+site.rva,site.bytes,site.size))return false;
 BYTE* block=(BYTE*)VirtualAlloc(nullptr,192,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);if(!block)return false;
 void* callbacks[]={(void*)repairOrochiRect,(void*)repairOrochiTriangle,(void*)repairOrochiQuad};
 for(unsigned i=0;i<3;i++){
  const auto& site=sites[i];BYTE* code=block+i*64;SIZE_T at=0;
  code[at++]=0x9c;code[at++]=0x60;code[at++]=0x51;code[at++]=0xb8;DWORD callback=(DWORD)(uintptr_t)callbacks[i];memcpy(code+at,&callback,4);at+=4;
  const BYTE invoke[]={0xff,0xd0,0x83,0xc4,4,0x61,0x9d};memcpy(code+at,invoke,sizeof(invoke));at+=sizeof(invoke);
  memcpy(code+at,site.bytes,site.size);at+=site.size;code[at++]=0xe9;LONG back=(LONG)((base+site.rva+site.size)-(code+at+4));memcpy(code+at,&back,4);at+=4;
  FlushInstructionCache(GetCurrentProcess(),code,at);
  BYTE jump[7]={0xe9,0,0,0,0,0x90,0x90};LONG forward=(LONG)(code-(base+site.rva+5));memcpy(jump+1,&forward,4);
  if(!replaceCode(base+site.rva,site.bytes,site.size,jump,site.size))return false;
 }
 DWORD old=0;VirtualProtect(block,192,PAGE_EXECUTE_READ,&old);
 logf("Orochi GS sprite Y overflow fix installed for internal height=%d",internalRenderHeight);return true;
}
static void patchGraphics() {
 if(!windowMode||graphicsPatched)return;
 // Orochi's engine needs the same config-driven resolution behavior, but its
 // getters/globals differ from SM6TC's immediate sites and must be patched by
 // this inspected-build path. Other games use the generic D3D9 hooks below.
 if(!nativeMenuMap)return;
 BYTE* base=(BYTE*)GetModuleHandleA(nullptr);
 // Verify all getters before modifying any of them.
 const DWORD getters[]={0x75d00,0x75d20,0x75d40,0x75d60};
 for(auto r:getters)if(memcmp(base+r,"\x0f\xb6\x05",3))return;
 const BYTE modeCheck[]={0x83,0xec,0x10,0x55,0x8b,0x6c,0x24,0x1c};
 if(memcmp(base+0x1cafc0,modeCheck,sizeof(modeCheck)))return;
 bool engineSizes=internalRenderWidth>=640&&internalRenderHeight>=360;
 const BYTE sizeLookup[]={0x8b,0x44,0x24,0x04,0x83,0xf8};
 const BYTE desktopCheck[]={0x8b,0x2d,0x74,0xc2,0x6c,0x00};
 if(engineSizes){
  for(DWORD r:{0x74470u,0x744d0u,0x74530u})if(memcmp(base+r,sizeLookup,sizeof(sizeLookup)))return;
  if(memcmp(base+0x1cb8f3,desktopCheck,sizeof(desktopCheck)))return;
 }
 bool ok=constantGetter(base,0x75d00,renderWidth)&&constantGetter(base,0x75d20,renderHeight)
  &&constantGetter(base,0x75d40,renderHeight)&&constantGetter(base,0x75d60,1);
 // Windowed buffers need not be physical fullscreen display modes. Actual
 // allocation/capability errors still come from Direct3D CreateDevice/Reset.
 const BYTE allowWindowedSize[]={0xb8,1,0,0,0,0xc2,8,0};
 ok=ok&&replaceCode(base+0x1cafc0,modeCheck,sizeof(modeCheck),allowWindowedSize,sizeof(allowWindowedSize));
 if(ok&&engineSizes){
  const DWORD lookups[]={0x74470,0x744d0,0x74530};
  for(unsigned i=0;i<3;i++){BYTE code[]={0xb8,0,0,0,0,0xc3};int value=i?renderHeight:renderWidth;memcpy(code+1,&value,4);ok=replaceCode(base+lookups[i],sizeLookup,sizeof(sizeLookup),code,sizeof(code))&&ok;}
  // A supersampled window render size need not fit the physical desktop.
  const BYTE skipDesktopFallback[]={0xe9,0xa9,0,0,0,0x90};
  ok=replaceCode(base+0x1cb8f3,desktopCheck,sizeof(desktopCheck),skipDesktopFallback,sizeof(skipDesktopFallback))&&ok;
 }
 if(ok&&engineSizes&&internalRenderHeight>2048)ok=installOrochiSpriteFix(base);
 if(ok){*(int*)(base+0x39fd74)=renderWidth;*(int*)(base+0x39fd78)=renderHeight;graphicsPatched=true;}
 logf("native render size getters %s %dx%d",ok?"patched":"FAILED",renderWidth,renderHeight);
}
static LRESULT CALLBACK platformWindowProcA(HWND h,UINT message,WPARAM w,LPARAM l) {
 if(windowMode&&h==gameWindow&&message==WM_STYLECHANGING&&l){
  STYLESTRUCT* style=(STYLESTRUCT*)l;
  if(w==GWL_STYLE)style->styleNew=(style->styleNew&~(DWORD)WS_OVERLAPPEDWINDOW)|WS_POPUP;
  else if(w==GWL_EXSTYLE)style->styleNew&=~(DWORD)(WS_EX_CLIENTEDGE|WS_EX_WINDOWEDGE|WS_EX_DLGMODALFRAME|WS_EX_TOPMOST);
 }
 if(windowMode&&h==gameWindow&&message==WM_WINDOWPOSCHANGING&&l){
  WINDOWPOS* pos=(WINDOWPOS*)l;pos->x=monitorBounds.left;pos->y=monitorBounds.top;
  pos->cx=monitorBounds.right-monitorBounds.left;pos->cy=monitorBounds.bottom-monitorBounds.top;
  pos->flags&=~(SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER);pos->hwndInsertAfter=HWND_NOTOPMOST;
 }
 if(message==WM_CLOSE&&quickClose){logf("WM_CLOSE: normal destroy without confirmation");releaseAllScaleSurfaces();releaseAllNativeOutputChains();if(IsWindow(h))DestroyWindow(h);return 0;}
 if(message==WM_DESTROY)releaseAllNativeOutputChains();
 if(message==WM_DEVICECHANGE){std::lock_guard<std::recursive_mutex> lock(mutex);nextSourceScan=0;logf("WM_DEVICECHANGE: source rescan scheduled");}
 if(message==WM_IME_SETCONTEXT&&imeIsolation)return DefWindowProcA(h,message,w,0);
 return CallWindowProcA(gameWindowProc,h,message,w,l);
}
static LRESULT CALLBACK platformWindowProcW(HWND h,UINT message,WPARAM w,LPARAM l) {
 if(windowMode&&h==gameWindow&&message==WM_STYLECHANGING&&l){
  STYLESTRUCT* style=(STYLESTRUCT*)l;
  if(w==GWL_STYLE)style->styleNew=(style->styleNew&~(DWORD)WS_OVERLAPPEDWINDOW)|WS_POPUP;
  else if(w==GWL_EXSTYLE)style->styleNew&=~(DWORD)(WS_EX_CLIENTEDGE|WS_EX_WINDOWEDGE|WS_EX_DLGMODALFRAME|WS_EX_TOPMOST);
 }
 if(windowMode&&h==gameWindow&&message==WM_WINDOWPOSCHANGING&&l){
  WINDOWPOS* pos=(WINDOWPOS*)l;pos->x=monitorBounds.left;pos->y=monitorBounds.top;
  pos->cx=monitorBounds.right-monitorBounds.left;pos->cy=monitorBounds.bottom-monitorBounds.top;
  pos->flags&=~(SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER);pos->hwndInsertAfter=HWND_NOTOPMOST;
 }
 if(message==WM_CLOSE&&quickClose){logf("WM_CLOSE: normal destroy without confirmation");releaseAllScaleSurfaces();releaseAllNativeOutputChains();if(IsWindow(h))DestroyWindow(h);return 0;}
 if(message==WM_DESTROY)releaseAllNativeOutputChains();
 if(message==WM_DEVICECHANGE){std::lock_guard<std::recursive_mutex> lock(mutex);nextSourceScan=0;logf("WM_DEVICECHANGE: source rescan scheduled");}
 if(message==WM_IME_SETCONTEXT&&imeIsolation)return DefWindowProcW(h,message,w,0);
 return CallWindowProcW(gameWindowProc,h,message,w,l);
}
static void logApartment(const char* stage) {
 auto getApartment=(HRESULT(WINAPI*)(int*,int*))GetProcAddress(GetModuleHandleA("ole32.dll"),"CoGetApartmentType");
 int type=-1,qualifier=-1;HRESULT hr=getApartment?getApartment(&type,&qualifier):E_NOTIMPL;
 logf("COM apartment %s hr=%08lx type=%d qualifier=%d HKL=%p",stage,hr,type,qualifier,GetKeyboardLayout(0));
}
static bool ignoredWindowClassA(LPCSTR cls) {
 if((ULONG_PTR)cls<=65535)return true;
 return !strcmp(cls,"#32770")||!strcmp(cls,"Static")||!strcmp(cls,"Button")||!strcmp(cls,"Edit")||!strcmp(cls,"ComboBox")||!strcmp(cls,"SysListView32");
}
static bool ignoredWindowClassW(LPCWSTR cls) {
 if((ULONG_PTR)cls<=65535)return true;
 return !wcscmp(cls,L"#32770")||!wcscmp(cls,L"Static")||!wcscmp(cls,L"Button")||!wcscmp(cls,L"Edit")||!wcscmp(cls,L"ComboBox")||!wcscmp(cls,L"SysListView32");
}
static bool chooseMainWindowA(LPCSTR cls,LPCSTR title,HWND parent) {
 if(gameWindow||parent||ignoredWindowClassA(cls))return false;
 if(cls&&(ULONG_PTR)cls>65535) {
  if(!strcmp(cls,"Orochi_Z_PC"))return true;
  // DW6 PC registers "SangokuMusou5PC"; matching it keeps the borderless
  // resize off any splash/launcher window the game creates first.
  if(!_stricmp(gameExeName,"SM5_WIN.exe")&&!strcmp(cls,"SangokuMusou5PC"))return true;
  if(!_stricmp(gameExeName,"ShinSangokumusou3.exe")&&!strcmp(cls,"KOEI-Dynasty Warriors 4 Window Class"))return true;
  if((!_stricmp(gameExeName,"SM4_WIN.exe")||!_stricmp(gameExeName,"SS4.exe")||!_stricmp(gameExeName,"SS4_Plus.exe")||!_stricmp(gameExeName,"SS4_Super.exe")||!_stricmp(gameExeName,"SS4_Ultima.exe"))&&!strcmp(cls,"SM4PC"))return true;
  if(!_stricmp(gameExeName,"SW2.exe")&&!strcmp(cls,"SW2WIN"))return true;
 }
 return title&&(ULONG_PTR)title>65535&&*title;
}
static bool chooseMainWindowW(LPCWSTR cls,LPCWSTR title,HWND parent) {
 if(gameWindow||parent||ignoredWindowClassW(cls))return false;
 if(cls&&(ULONG_PTR)cls>65535&&!_stricmp(gameExeName,"Launch.exe"))return true;
 return title&&(ULONG_PTR)title>65535&&*title;
}
static void configureMainWindow(HWND h,bool unicode,int x,int y,int width,int height) {
 gameWindow=h;gameWindowProc=(WNDPROC)(unicode?SetWindowLongPtrW(h,GWLP_WNDPROC,(LONG_PTR)platformWindowProcW):SetWindowLongPtrA(h,GWLP_WNDPROC,(LONG_PTR)platformWindowProcA));
 char className[128]={};GetClassNameA(h,className,(int)sizeof(className));
 logApartment("after window");
 if(associateIme)associateIme(h,nullptr,0);
 if(windowMode){
  LONG_PTR style=GetWindowLongPtrA(h,GWL_STYLE);style=(style&~(LONG_PTR)WS_OVERLAPPEDWINDOW)|WS_POPUP;SetWindowLongPtrA(h,GWL_STYLE,style);
  LONG_PTR exStyle=GetWindowLongPtrA(h,GWL_EXSTYLE);exStyle&=~(LONG_PTR)(WS_EX_CLIENTEDGE|WS_EX_WINDOWEDGE|WS_EX_DLGMODALFRAME|WS_EX_TOPMOST);SetWindowLongPtrA(h,GWL_EXSTYLE,exStyle);
  x=monitorBounds.left;y=monitorBounds.top;width=monitorBounds.right-x;height=monitorBounds.bottom-y;
  SetWindowPos(h,HWND_NOTOPMOST,x,y,width,height,SWP_FRAMECHANGED|SWP_NOACTIVATE);
 }
 else SetWindowPos(h,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
 logf("main window hooked hwnd=%p class=%s unicode=%d bounds=%d,%d %dx%d",h,className,unicode,x,y,width,height);
}
static BOOL CALLBACK findExistingSW2Window(HWND h,LPARAM) {
 if(gameWindow)return FALSE;
 DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(pid!=GetCurrentProcessId())return TRUE;
 char cls[128]={};if(!GetClassNameA(h,cls,(int)sizeof(cls))||strcmp(cls,"SW2WIN"))return TRUE;
 RECT r={};if(!GetWindowRect(h,&r))return TRUE;
 configureMainWindow(h,IsWindowUnicode(h),r.left,r.top,r.right-r.left,r.bottom-r.top);
 return FALSE;
}
static void attachExistingMainWindow() {
 if(gameWindow||_stricmp(gameExeName,"SW2.exe"))return;
 EnumWindows(findExistingSW2Window,0);
}
// Some builds resolve user32 at runtime instead of importing it, so the
// CreateWindowExA IAT hook never installs and the main window is never seen.
// DW6 PC is one of them (its import table lists a single user32 function).
// Fall back to polling the process's own top-level windows for the class the
// game registers; once found, the normal subclassing and borderless resize run.
static const char* expectedMainWindowClass() {
 if(!_stricmp(gameExeName,"SM5_WIN.exe"))return "SangokuMusou5PC";
 if(!_stricmp(gameExeName,"OROCHI_Z_TC.exe"))return "Orochi_Z_PC";
 if(!_stricmp(gameExeName,"SW2.exe"))return "SW2WIN";
 if(!_stricmp(gameExeName,"ShinSangokumusou3.exe"))return "KOEI-Dynasty Warriors 4 Window Class";
 if(!_stricmp(gameExeName,"SM4_WIN.exe")||!_stricmp(gameExeName,"SS4.exe")||!_stricmp(gameExeName,"SS4_Plus.exe")||
    !_stricmp(gameExeName,"SS4_Super.exe")||!_stricmp(gameExeName,"SS4_Ultima.exe"))return "SM4PC";
 return nullptr;
}
static BOOL CALLBACK findMainWindowByClass(HWND h,LPARAM param) {
 DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(pid!=GetCurrentProcessId())return TRUE;
 if(!IsWindowVisible(h))return TRUE;
 char cls[128]={};if(!GetClassNameA(h,cls,(int)sizeof(cls)))return TRUE;
 const char* wanted=expectedMainWindowClass();
 if(!wanted||_stricmp(cls,wanted))return TRUE;
 RECT r={};if(!GetWindowRect(h,&r))return TRUE;
 // Skip splash/empty windows: the real game window always has a usable size.
 if(r.right-r.left<160||r.bottom-r.top<120)return TRUE;
 configureMainWindow(h,IsWindowUnicode(h),r.left,r.top,r.right-r.left,r.bottom-r.top);
 *(HWND*)param=h;
 return FALSE;
}
// Diagnostic: report every distinct top-level class this process shows, once, so
// a wrong expectedMainWindowClass() guess is visible in the log instead of
// looking like "the window never appeared".
static char watcherSeenClass[8][64]={};
static int watcherSeenCount=0;
static BOOL CALLBACK logProcessWindowClasses(HWND h,LPARAM) {
 DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(pid!=GetCurrentProcessId())return TRUE;
 if(!IsWindowVisible(h))return TRUE;
 char cls[128]={};if(!GetClassNameA(h,cls,(int)sizeof(cls)))return TRUE;
 for(int i=0;i<watcherSeenCount;i++)if(!_stricmp(watcherSeenClass[i],cls))return TRUE;
 RECT r={};GetWindowRect(h,&r);
 if(watcherSeenCount<8){strcpy_s(watcherSeenClass[watcherSeenCount++],cls);
  logf("main window candidate class=%s size=%dx%d",cls,r.right-r.left,r.bottom-r.top);}
 return TRUE;
}
// Fallback: the single large visible window the process owns. Used only when the
// expected class never shows up, so an unpredictable class name still gets the
// borderless treatment instead of silently doing nothing.
struct WindowPick { HWND hwnd; int count; };
static BOOL CALLBACK pickLargeProcessWindow(HWND h,LPARAM param) {
 WindowPick* p=(WindowPick*)param;
 DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(pid!=GetCurrentProcessId())return TRUE;
 if(!IsWindowVisible(h))return TRUE;
 char cls[128]={};if(!GetClassNameA(h,cls,(int)sizeof(cls)))return TRUE;
 if(!_stricmp(cls,"IME")||!_stricmp(cls,"MSCTFIME UI")||!_stricmp(cls,"Default IME"))return TRUE;
 RECT r={};if(!GetWindowRect(h,&r))return TRUE;
 if(r.right-r.left<640||r.bottom-r.top<360)return TRUE;
 p->count++;
 if(!p->hwnd)p->hwnd=h;
 return TRUE;
}
static DWORD WINAPI mainWindowWatch(void*) {
 SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
 for(int attempt=0;attempt<600&&!gameWindow;attempt++){
  HWND found=nullptr;EnumWindows(findMainWindowByClass,(LPARAM)&found);
  if(found)break;
  EnumWindows(logProcessWindowClasses,0);
  if(attempt>=50){
   WindowPick pick={};EnumWindows(pickLargeProcessWindow,(LPARAM)&pick);
   if(pick.count==1&&pick.hwnd){
    RECT r={};GetWindowRect(pick.hwnd,&r);
    char cls[128]={};GetClassNameA(pick.hwnd,cls,(int)sizeof(cls));
    logf("main window class mismatch: expected=%s actual=%s; attaching anyway",expectedMainWindowClass(),cls);
    configureMainWindow(pick.hwnd,IsWindowUnicode(pick.hwnd),r.left,r.top,r.right-r.left,r.bottom-r.top);
    break;
   }
  }
  Sleep(100);
 }
 if(gameWindow)logf("main window attached by class watcher class=%s",expectedMainWindowClass());
 else logf("main window watcher gave up: no %s window appeared",expectedMainWindowClass());
 return 0;
}
static void startMainWindowWatch() {
 // Only needed when the CreateWindowExA/CreateWindowExW IAT slots could not be
 // patched (the game resolves user32 dynamically) and the exe has a known class.
 if(!windowMode||gameWindow||(earlyHookMask&3)||!expectedMainWindowClass())return;
 HANDLE thread=CreateThread(nullptr,0,mainWindowWatch,nullptr,0,nullptr);
 if(!thread){logf("main window watcher failed to start error=%lu",GetLastError());return;}
 CloseHandle(thread);
 logf("main window watcher started for class=%s",expectedMainWindowClass());
}
static HWND WINAPI platformCreateWindow(DWORD ex,LPCSTR cls,LPCSTR title,DWORD style,int x,int y,int width,int height,HWND parent,HMENU menu,HINSTANCE inst,LPVOID param) {
 initializeAll();patchSsm7Resolution();bool target=chooseMainWindowA(cls,title,parent);
 if(target){
  logApartment("before window");
  if(disableIme)logf("ImmDisableIME before main window result=%d",disableIme((DWORD)-1));
  patchGraphics();ex&=~WS_EX_TOPMOST;
  if(windowMode){ex&=~(WS_EX_CLIENTEDGE|WS_EX_WINDOWEDGE|WS_EX_DLGMODALFRAME);style=(style&~(WS_OVERLAPPEDWINDOW))|WS_POPUP;
   x=monitorBounds.left;y=monitorBounds.top;width=monitorBounds.right-x;height=monitorBounds.bottom-y;}
 }
 HWND h=realCreateWindow?realCreateWindow(ex,cls,title,style,x,y,width,height,parent,menu,inst,param):nullptr;
 if(target&&h)configureMainWindow(h,false,x,y,width,height);
 return h;
}
static HWND WINAPI platformCreateWindowW(DWORD ex,LPCWSTR cls,LPCWSTR title,DWORD style,int x,int y,int width,int height,HWND parent,HMENU menu,HINSTANCE inst,LPVOID param) {
 initializeAll();patchSsm7Resolution();bool target=chooseMainWindowW(cls,title,parent);
 if(target){
  logApartment("before window");
  if(disableIme)logf("ImmDisableIME before main window result=%d",disableIme((DWORD)-1));
  patchGraphics();ex&=~WS_EX_TOPMOST;
  if(windowMode){ex&=~(WS_EX_CLIENTEDGE|WS_EX_WINDOWEDGE|WS_EX_DLGMODALFRAME);style=(style&~(WS_OVERLAPPEDWINDOW))|WS_POPUP;
   x=monitorBounds.left;y=monitorBounds.top;width=monitorBounds.right-x;height=monitorBounds.bottom-y;}
 }
 HWND h=realCreateWindowW?realCreateWindowW(ex,cls,title,style,x,y,width,height,parent,menu,inst,param):nullptr;
 if(target&&h)configureMainWindow(h,true,x,y,width,height);
 return h;
}
static BOOL WINAPI platformSetWindowPos(HWND h,HWND after,int x,int y,int width,int height,UINT flags) {
 if(h==gameWindow){
  if(after==HWND_TOPMOST)after=HWND_NOTOPMOST;
  if(windowMode){x=monitorBounds.left;y=monitorBounds.top;width=monitorBounds.right-x;height=monitorBounds.bottom-y;
   flags&=~(SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER);after=HWND_NOTOPMOST;}
 }
 return realSetWindowPos(h,after,x,y,width,height,flags);
}
static LONG WINAPI platformSetWindowLong(HWND h,int index,LONG value) {
 if(h==gameWindow){if(index==GWL_EXSTYLE)value&=~WS_EX_TOPMOST;
  if(windowMode&&index==GWL_STYLE)value=(value&~WS_OVERLAPPEDWINDOW)|WS_POPUP;
  if(windowMode&&index==GWL_EXSTYLE)value&=~(WS_EX_CLIENTEDGE|WS_EX_WINDOWEDGE|WS_EX_DLGMODALFRAME);}
 return realSetWindowLong?realSetWindowLong(h,index,value):0;
}
static LONG WINAPI platformSetWindowLongW(HWND h,int index,LONG value) {
 if(h==gameWindow){if(index==GWL_EXSTYLE)value&=~WS_EX_TOPMOST;
  if(windowMode&&index==GWL_STYLE)value=(value&~WS_OVERLAPPEDWINDOW)|WS_POPUP;
  if(windowMode&&index==GWL_EXSTYLE)value&=~(WS_EX_CLIENTEDGE|WS_EX_WINDOWEDGE|WS_EX_DLGMODALFRAME);}
 return realSetWindowLongW?realSetWindowLongW(h,index,value):0;
}
static BOOL WINAPI platformClipCursor(const RECT* rect){return realClipCursor(sharedInput?nullptr:rect);}
static bool containsTextInsensitive(const wchar_t* text,const wchar_t* needle) {
 if(!text||!needle||!*needle)return false;size_t n=wcslen(needle);
 for(const wchar_t* p=text;*p;p++)if(!_wcsnicmp(p,needle,n))return true;return false;
}
static bool isExitConfirmation(const wchar_t* text) {
 return containsTextInsensitive(text,L"结束游戏")||containsTextInsensitive(text,L"退出游戏")||
  containsTextInsensitive(text,L"返回Windows")||containsTextInsensitive(text,L"返回Ｗｉｎｄｏｗｓ")||containsTextInsensitive(text,L"return to windows")||
  containsTextInsensitive(text,L"quit game")||containsTextInsensitive(text,L"exit game");
}
static int WINAPI platformMessageBox(HWND h,LPCSTR text,LPCSTR title,UINT flags) {
 initializeAll();char hex[161]={};if(text)for(size_t i=0;i<80&&text[i];i++)sprintf_s(hex+i*2,sizeof(hex)-i*2,"%02x",(unsigned char)text[i]);
 logf("MessageBox flags=%x caller=%p text_hex=%s",flags,_ReturnAddress(),hex);
 if(quickClose&&text){wchar_t wide[1024]={};if(MultiByteToWideChar(CP_ACP,0,text,-1,wide,1024)&&isExitConfirmation(wide)){logf("suppressed exit confirmation MessageBoxA");return IDYES;}}
 // SM3 displays a static MyCrack credit box before creating its main window.
 // It is informational and should not block startup when close_without_confirmation is enabled.
 if(quickClose&&text&&strstr(text,"MyCrack")){logf("suppressed SM3 startup credit box");return IDOK;}
 // This build's UI strings are CP950. Decode only its own static strings.
 BYTE* base=(BYTE*)GetModuleHandleA(nullptr);auto nt=(IMAGE_NT_HEADERS*)(base+((IMAGE_DOS_HEADER*)base)->e_lfanew);
 if(!_stricmp(gameExeName,"OROCHI_Z_TC.exe")&&(const BYTE*)text>=base&&(const BYTE*)text<base+nt->OptionalHeader.SizeOfImage){
  wchar_t wt[1024]={},wc[256]={};MultiByteToWideChar(950,0,text,-1,wt,1024);MultiByteToWideChar(950,0,title?title:"",-1,wc,256);
  return MessageBoxW(h,wt,wc,flags);
 }
 return realMessageBox?realMessageBox(h,text,title,flags):0;
}
static int WINAPI platformMessageBoxW(HWND h,LPCWSTR text,LPCWSTR title,UINT flags) {
 initializeAll();
 if(quickClose&&isExitConfirmation(text)){logf("suppressed exit confirmation MessageBoxW");return IDYES;}
 return realMessageBoxW?realMessageBoxW(h,text,title,flags):0;
}
static HRESULT finishCoInitialize(LPVOID reserved,DWORD model,HRESULT hr) {
 if(comCompatibility&&hr==RPC_E_CHANGED_MODE&&model==COINIT_MULTITHREADED){
  HRESULT retry=realCoInitialize(reserved,COINIT_APARTMENTTHREADED);
  logf("COM existing STA retained with balanced reference hr=%08lx",retry);return retry;
 }
 return hr;
}
static HRESULT WINAPI platformCoInitialize(LPVOID reserved,DWORD model) {
 initializeAll();HRESULT hr=realCoInitialize(reserved,model);void* caller=_ReturnAddress();
 logf("CoInitializeEx model=%lu hr=%08lx caller=%p",model,hr,caller);
 return finishCoInitialize(reserved,model,hr);
}
static HRESULT WINAPI platformCoInitializeLegacy(LPVOID reserved) {
 initializeAll();HRESULT hr=realCoInitializeLegacy?realCoInitializeLegacy(reserved):E_FAIL;
 logf("CoInitialize hr=%08lx",hr);return hr;
}
static FARPROC WINAPI platformGetProcAddress(HMODULE module,LPCSTR name) {
 if(!realGetProcAddress)return nullptr;
 if((ULONG_PTR)name>65535&&module==GetModuleHandleA("d3d9.dll")&&!_stricmp(name,"Direct3DCreate9")){
  realCreateD3D=(decltype(realCreateD3D))realGetProcAddress(module,name);
  logf("dynamic API hook: d3d9!Direct3DCreate9");return (FARPROC)platformCreateD3D;
 }
 if((ULONG_PTR)name>65535&&module==GetModuleHandleA("user32.dll")) {
  if(!_stricmp(name,"CreateWindowExA")){logf("dynamic API hook: user32!%s",name);realCreateWindow=(decltype(realCreateWindow))realGetProcAddress(module,name);return (FARPROC)platformCreateWindow;}
  if(!_stricmp(name,"CreateWindowExW")){realCreateWindowW=(decltype(realCreateWindowW))realGetProcAddress(module,name);return (FARPROC)platformCreateWindowW;}
  if(!_stricmp(name,"SetWindowPos")){realSetWindowPos=(decltype(realSetWindowPos))realGetProcAddress(module,name);return (FARPROC)platformSetWindowPos;}
  if(!_stricmp(name,"SetWindowLongA")){realSetWindowLong=(decltype(realSetWindowLong))realGetProcAddress(module,name);return (FARPROC)platformSetWindowLong;}
  if(!_stricmp(name,"SetWindowLongW")){realSetWindowLongW=(decltype(realSetWindowLongW))realGetProcAddress(module,name);return (FARPROC)platformSetWindowLongW;}
  if(!_stricmp(name,"ClipCursor")){realClipCursor=(decltype(realClipCursor))realGetProcAddress(module,name);return (FARPROC)platformClipCursor;}
  if(!_stricmp(name,"MessageBoxA")){realMessageBox=(decltype(realMessageBox))realGetProcAddress(module,name);return (FARPROC)platformMessageBox;}
  if(!_stricmp(name,"MessageBoxW")){realMessageBoxW=(decltype(realMessageBoxW))realGetProcAddress(module,name);return (FARPROC)platformMessageBoxW;}
 }
 return realGetProcAddress(module,name);
}
static bool patchImport(const char* dllName,const char* functionName,void* hook,void** original) {
 BYTE* base=(BYTE*)GetModuleHandleA(nullptr);auto dos=(IMAGE_DOS_HEADER*)base;
 if(!base||dos->e_magic!=IMAGE_DOS_SIGNATURE)return false;
 auto nt=(IMAGE_NT_HEADERS*)(base+dos->e_lfanew);auto& dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
 if(!dir.VirtualAddress)return false;
 auto desc=(IMAGE_IMPORT_DESCRIPTOR*)(base+dir.VirtualAddress);
 for(;desc->Name;desc++) {
  const char* name=(const char*)(base+desc->Name);if(_stricmp(name,dllName)!=0)continue;
  // Some packed builds share INT and IAT; the loader has already replaced
  // their name RVAs with resolved addresses. Match those slots by API address.
  auto names=desc->OriginalFirstThunk&&desc->OriginalFirstThunk!=desc->FirstThunk?(IMAGE_THUNK_DATA*)(base+desc->OriginalFirstThunk):nullptr;
  auto slots=(IMAGE_THUNK_DATA*)(base+desc->FirstThunk);
  FARPROC resolved=GetProcAddress(GetModuleHandleA(dllName),functionName);
  for(DWORD i=0;slots[i].u1.Function;i++) {
   bool match=false;
   if(names&&names[i].u1.AddressOfData&&!IMAGE_SNAP_BY_ORDINAL(names[i].u1.Ordinal)) {
    auto ibn=(IMAGE_IMPORT_BY_NAME*)(base+names[i].u1.AddressOfData);match=_stricmp((const char*)ibn->Name,functionName)==0;
   } else if(!names&&resolved)match=(FARPROC)(ULONG_PTR)slots[i].u1.Function==resolved;
   if(!match)continue;
   auto slot=&slots[i].u1.Function;
   DWORD old=0;if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old))return false;
   if(original&&!*original)*original=(void*)(ULONG_PTR)*slot;
   InterlockedExchangePointer((PVOID*)slot,hook);DWORD discard;VirtualProtect(slot,sizeof(void*),old,&discard);return true;
  }
 }
 return false;
}
static void installDeferredGetProcHook() {
 if(deferGetProcHook&&patchImport("kernel32.dll","GetProcAddress",(void*)platformGetProcAddress,(void**)&realGetProcAddress)){
  deferGetProcHook=false;logf("deferred GetProcAddress hook installed after DirectInput startup");
 }
}
[[maybe_unused]] static void installEarlyHooks() {
 char exe[MAX_PATH]={};GetModuleFileNameA(nullptr,exe,MAX_PATH);const char* name=strrchr(exe,'\\');name=name?name+1:exe;
 deferGetProcHook=!_stricmp(name,"SW2.exe");
 struct Entry{const char* dll;const char* name;void* hook;void** original;};
 Entry hooks[]={
  {"user32.dll","CreateWindowExA",(void*)platformCreateWindow,(void**)&realCreateWindow},
  {"user32.dll","CreateWindowExW",(void*)platformCreateWindowW,(void**)&realCreateWindowW},
  {"user32.dll","SetWindowPos",(void*)platformSetWindowPos,(void**)&realSetWindowPos},
  {"user32.dll","SetWindowLongA",(void*)platformSetWindowLong,(void**)&realSetWindowLong},
  {"user32.dll","SetWindowLongW",(void*)platformSetWindowLongW,(void**)&realSetWindowLongW},
  {"user32.dll","ClipCursor",(void*)platformClipCursor,(void**)&realClipCursor},
  {"user32.dll","MessageBoxA",(void*)platformMessageBox,(void**)&realMessageBox},
  {"user32.dll","MessageBoxW",(void*)platformMessageBoxW,(void**)&realMessageBoxW},
  {"ole32.dll","CoInitializeEx",(void*)platformCoInitialize,(void**)&realCoInitialize},
  {"ole32.dll","CoInitialize",(void*)platformCoInitializeLegacy,(void**)&realCoInitializeLegacy},
  {"d3d9.dll","Direct3DCreate9",(void*)platformCreateD3D,(void**)&realCreateD3D},
  {"kernel32.dll","GetProcAddress",(void*)platformGetProcAddress,(void**)&realGetProcAddress}};
 for(size_t i=0;i<sizeof(hooks)/sizeof(hooks[0]);i++){auto& hook=hooks[i];if(deferGetProcHook&&!strcmp(hook.name,"GetProcAddress"))continue;if(patchImport(hook.dll,hook.name,hook.hook,hook.original)){earlyHookCount++;if(i<32)earlyHookMask|=1u<<i;}}
}
