// Included by padfix.cpp. All hooks are local to this executable's IAT.
#include <d3d9.h>
#include <d3dcompiler.h>
#include <objbase.h>
#include <intrin.h>
#include <unordered_map>
#include <unordered_set>
#include <cwchar>
#include <cctype>
static void initializeAll();
static void attachExistingMainWindow();
static int sharedInput=1,windowMode=1,imeIsolation=1,comCompatibility=1,quickClose=1;
static int fpsLimit=0;
static int renderWidth=0,renderHeight=0;
static bool nativeResolution=false,scaleToFill=false;
// 0=point, 1=linear, 2=Catmull-Rom cubic reconstruction.
static int scaleFilter=2;
static HWND gameWindow;
static WNDPROC gameWindowProc;
static RECT monitorBounds;
static int earlyHookCount;
static unsigned earlyHookMask;
static bool graphicsPatched=false;
static bool forceNativeBuffer=false;
static bool deferGetProcHook=false;
static bool lateD3DHookAttempted=false;
static char gameExeName[MAX_PATH];
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
struct TableHook {void* release;std::unordered_map<size_t,void*> methods;};
static std::mutex tableMutex;
static std::unordered_map<void*,TableHook> tables;
static std::unordered_set<void*> renderDevices;
static std::unordered_map<void*,std::pair<UINT,UINT>> logicalRenderSizes;
static std::unordered_map<void*,std::pair<UINT,UINT>> loggedNativeViewports,loggedNativeScissors;
static std::unordered_map<void*,IDirect3DSurface9*> scaleSurfaces;
static std::unordered_map<void*,IDirect3DSwapChain9*> nativeOutputChains;
struct NativeScaleSize {UINT width,height;D3DFORMAT format;};
static std::unordered_map<void*,IDirect3DTexture9*> nativeScaleTextures;
static std::unordered_map<void*,IDirect3DPixelShader9*> nativeScaleShaders;
static std::unordered_map<void*,NativeScaleSize> nativeScaleSizes;
static std::unordered_set<void*> nativeScaleFailed;
static std::unordered_set<void*> nativeOutputDevices,loggedNativeOutputDevices,loggedNativeOutputFailures;
static std::unordered_set<void*> loggedScaleDevices,loggedScaleFailures;
static void* originalMethod(void* object,size_t index) {
 std::lock_guard<std::mutex> lock(tableMutex);auto i=tables.find(*(void***)object);
 if(i==tables.end())return nullptr;
 if(index==2)return i->second.release;
 auto method=i->second.methods.find(index);return method==i->second.methods.end()?nullptr:method->second;
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
 return nativeResolution&&scaleToFill&&gameWindow&&p.hDeviceWindow==gameWindow&&!_stricmp(gameExeName,"Launch.exe");
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
static bool acquireNativeScaleResources(IDirect3DDevice9* device,const D3DSURFACE_DESC& source,IDirect3DTexture9** textureOut,IDirect3DPixelShader9** shaderOut) {
 if(!device||!textureOut||!shaderOut)return false;*textureOut=nullptr;*shaderOut=nullptr;
 std::lock_guard<std::mutex> lock(tableMutex);
 if(nativeScaleFailed.count(device))return false;
 auto size=nativeScaleSizes.find(device);auto texture=nativeScaleTextures.find(device);
 if(texture!=nativeScaleTextures.end()&&(size==nativeScaleSizes.end()||size->second.width!=source.Width||size->second.height!=source.Height||size->second.format!=source.Format)){
  texture->second->Release();nativeScaleTextures.erase(texture);nativeScaleSizes.erase(device);texture=nativeScaleTextures.end();
 }
 if(texture==nativeScaleTextures.end()){
  IDirect3DTexture9* created=nullptr;HRESULT hr=device->CreateTexture(source.Width,source.Height,1,D3DUSAGE_RENDERTARGET,source.Format,D3DPOOL_DEFAULT,&created,nullptr);
  if(FAILED(hr)||!created){nativeScaleFailed.insert(device);logf("D3D cubic source texture creation failed size=%ux%u format=%u hr=%08lx",source.Width,source.Height,source.Format,hr);return false;}
  nativeScaleTextures[device]=created;nativeScaleSizes[device]={source.Width,source.Height,source.Format};texture=nativeScaleTextures.find(device);
 }
 auto shader=nativeScaleShaders.find(device);
 if(shader==nativeScaleShaders.end()){
  IDirect3DPixelShader9* created=createNativeScaleShader(device);if(!created){nativeScaleFailed.insert(device);return false;}
  nativeScaleShaders[device]=created;shader=nativeScaleShaders.find(device);
 }
 *textureOut=texture->second;(*textureOut)->AddRef();*shaderOut=shader->second;(*shaderOut)->AddRef();return true;
}
static bool cubicScaleToOutput(IDirect3DDevice9* device,IDirect3DSurface9* source,IDirect3DSurface9* output) {
 D3DSURFACE_DESC src={},dst={};if(FAILED(source->GetDesc(&src))||FAILED(output->GetDesc(&dst))||!src.Width||!src.Height||!dst.Width||!dst.Height)return false;
 IDirect3DTexture9* texture=nullptr;IDirect3DPixelShader9* shader=nullptr;
 if(!acquireNativeScaleResources(device,src,&texture,&shader))return false;
 IDirect3DSurface9* textureSurface=nullptr;HRESULT copyHr=texture->GetSurfaceLevel(0,&textureSurface);
 if(SUCCEEDED(copyHr))copyHr=device->StretchRect(source,nullptr,textureSurface,nullptr,D3DTEXF_NONE);
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
  float size[4]={(float)src.Width,(float)src.Height,0.0f,0.0f};
  float inverse[4]={1.0f/(float)src.Width,1.0f/(float)src.Height,0.0f,0.0f};
  drawHr=device->SetPixelShaderConstantF(0,size,1);if(FAILED(drawHr))failedStage="SetPixelShaderConstantF0";if(SUCCEEDED(drawHr)){drawHr=device->SetPixelShaderConstantF(1,inverse,1);if(FAILED(drawHr))failedStage="SetPixelShaderConstantF1";}
 }
 if(SUCCEEDED(drawHr)){
  device->SetRenderState(D3DRS_ZENABLE,FALSE);device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);device->SetRenderState(D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_RED|D3DCOLORWRITEENABLE_GREEN|D3DCOLORWRITEENABLE_BLUE|D3DCOLORWRITEENABLE_ALPHA);
  device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);device->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);device->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);device->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);device->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
  struct Vertex {float x,y,z,rhw,u,v;};float right=(float)dst.Width-0.5f,bottom=(float)dst.Height-0.5f;
  Vertex quad[4]={{-0.5f,-0.5f,0.0f,1.0f,0.0f,0.0f},{right,-0.5f,0.0f,1.0f,1.0f,0.0f},{-0.5f,bottom,0.0f,1.0f,0.0f,1.0f},{right,bottom,0.0f,1.0f,1.0f,1.0f}};
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
 IDirect3DSurface9* surface=nullptr;
 {std::lock_guard<std::mutex> lock(tableMutex);auto i=scaleSurfaces.find(object);if(i!=scaleSurfaces.end()){surface=i->second;scaleSurfaces.erase(i);}loggedScaleDevices.erase(object);loggedScaleFailures.erase(object);}
 if(surface)surface->Release();
}
static void releaseAllScaleSurfaces() {
 std::vector<IDirect3DSurface9*> surfaces;
 {std::lock_guard<std::mutex> lock(tableMutex);for(auto& item:scaleSurfaces)surfaces.push_back(item.second);scaleSurfaces.clear();loggedScaleDevices.clear();loggedScaleFailures.clear();}
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
static void scaleBackbufferToNative(IDirect3DDevice9* d,const RECT* source,const RECT* destination) {
 if(!nativeResolution||!scaleToFill||source||destination)return;
 UINT logicalW=0,logicalH=0;{std::lock_guard<std::mutex> lock(tableMutex);auto i=logicalRenderSizes.find(d);if(i!=logicalRenderSizes.end()){logicalW=i->second.first;logicalH=i->second.second;}}
 if(!logicalW||!logicalH)return;
 IDirect3DSurface9* back=nullptr;if(FAILED(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back))||!back)return;
 D3DSURFACE_DESC desc={};HRESULT descHr=back->GetDesc(&desc);
 if(FAILED(descHr)||desc.Width<=logicalW||desc.Height<=logicalH){back->Release();return;}
 IDirect3DSurface9* temp=acquireScaleSurface(d,logicalW,logicalH,desc.Format);
 if(!temp){back->Release();return;}
 RECT crop={0,0,(LONG)logicalW,(LONG)logicalH};
 HRESULT copyHr=d->StretchRect(back,&crop,temp,nullptr,D3DTEXF_NONE);
 HRESULT scaleHr=FAILED(copyHr)?copyHr:d->StretchRect(temp,nullptr,back,nullptr,standardScaleFilter());
 bool logAttempt=false,logFailure=false;
 {std::lock_guard<std::mutex> lock(tableMutex);logAttempt=loggedScaleDevices.insert(d).second;if(FAILED(copyHr)||FAILED(scaleHr))logFailure=loggedScaleFailures.insert(d).second;}
 if(logAttempt||logFailure)logf("D3D scale frame source=%ux%u destination=%ux%u filter=%u copy_hr=%08lx scale_hr=%08lx",logicalW,logicalH,desc.Width,desc.Height,scaleFilter,copyHr,scaleHr);
 temp->Release();back->Release();
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
 auto fn=(ULONG(WINAPI*)(void*))originalMethod(object,2);if(!fn)return 0;
 ULONG refs=fn(object);
 if(!refs){releaseScaleSurface(object);releaseNativeOutputChain(object);std::lock_guard<std::mutex> lock(tableMutex);renderDevices.erase(object);logicalRenderSizes.erase(object);nativeOutputDevices.erase(object);loggedNativeViewports.erase(object);loggedNativeScissors.erase(object);}
 return refs;
}
static void hookTable(void** table,size_t index,void* hook) {
 if(!table)return;
 // Patch only the requested private vtable slots; never truncate/copy them.
 std::lock_guard<std::mutex> lock(tableMutex);auto& entry=tables[table];
 if(!entry.release)entry.release=table[2];
 if(!entry.methods.count(index))entry.methods[index]=table[index];
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
 // Older Koei games keep their UI viewport at the selected game resolution.
 // Keep that buffer unless the profile explicitly asks for native rendering.
 if(nativeResolution&&!needsNativeOutputChain(p))getNativeDisplaySize(p.BackBufferWidth,p.BackBufferHeight);
 else if(forceNativeBuffer&&!needsNativeOutputChain(p)){p.BackBufferWidth=renderWidth;p.BackBufferHeight=renderHeight;}
 // An explicit game frame limit owns the cadence; do not add a second wait
 // tied to the desktop refresh rate. Windowed mode leaves the monitor mode intact.
 if(fpsLimit>0)p.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
 else if(p.PresentationInterval!=D3DPRESENT_INTERVAL_IMMEDIATE)p.PresentationInterval=D3DPRESENT_INTERVAL_ONE;
}
static HRESULT WINAPI platformReset(IDirect3DDevice9* d,D3DPRESENT_PARAMETERS* p) {
 if(!p)return D3DERR_INVALIDCALL;
 auto fn=(HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*))originalMethod(d,16);
 bool ours=false,output=false;{std::lock_guard<std::mutex> lock(tableMutex);ours=renderDevices.count(d)!=0;output=nativeOutputDevices.count(d)!=0;}
 if(!ours&&gameWindow&&(windowMode||fpsLimit>0)){
  D3DDEVICE_CREATION_PARAMETERS cp={};
  auto get=(HRESULT(WINAPI*)(IDirect3DDevice9*,D3DDEVICE_CREATION_PARAMETERS*))(*(void***)d)[9];
  if(get&&SUCCEEDED(get(d,&cp))&&cp.hFocusWindow==gameWindow){ours=true;std::lock_guard<std::mutex> lock(tableMutex);renderDevices.insert(d);}
 }
 UINT requestedW=p->BackBufferWidth,requestedH=p->BackBufferHeight;BOOL requestedWindowed=p->Windowed;
 if(ours){releaseScaleSurface(d);releaseNativeOutputChain(d);if(output){p->Windowed=TRUE;p->FullScreen_RefreshRateInHz=0;if(p->PresentationInterval!=D3DPRESENT_INTERVAL_IMMEDIATE)p->PresentationInterval=D3DPRESENT_INTERVAL_ONE;}else forcePresentation(*p);}HRESULT hr=fn?fn(d,p):D3DERR_INVALIDCALL;
 if(ours&&nativeResolution&&SUCCEEDED(hr)&&requestedW&&requestedH){std::lock_guard<std::mutex> lock(tableMutex);logicalRenderSizes[d]={requestedW,requestedH};}
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
 auto fn=(HRESULT(WINAPI*)(IDirect3DDevice9*,const RECT*))originalMethod(d,64);
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
static HRESULT WINAPI platformPresent(IDirect3DDevice9* d,const RECT* source,const RECT* destination,HWND overrideWindow,const RGNDATA* dirty) {
 auto fn=(HRESULT(WINAPI*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*))originalMethod(d,17);
 IDirect3DSwapChain9* chain=nullptr;{std::lock_guard<std::mutex> lock(tableMutex);auto i=nativeOutputChains.find(d);if(i!=nativeOutputChains.end()){chain=i->second;chain->AddRef();}}
 HRESULT copyHr=E_FAIL;bool cubicUsed=false;
 if(chain){
  IDirect3DSurface9* from=nullptr;IDirect3DSurface9* to=nullptr;
  if(SUCCEEDED(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&from))&&from&&SUCCEEDED(chain->GetBackBuffer(0,D3DBACKBUFFER_TYPE_MONO,&to))&&to)
   if(scaleFilter==2&&cubicScaleToOutput(d,from,to)){copyHr=S_OK;cubicUsed=true;}
   else copyHr=d->StretchRect(from,nullptr,to,nullptr,standardScaleFilter());
  if(from)from->Release();if(to)to->Release();
 }else scaleBackbufferToNative(d,source,destination);
 HRESULT hr=fn?fn(d,source,destination,overrideWindow,dirty):D3DERR_INVALIDCALL;
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
static HRESULT WINAPI platformCreateDevice(IDirect3D9* d,UINT adapter,D3DDEVTYPE type,HWND focus,DWORD flags,D3DPRESENT_PARAMETERS* p,IDirect3DDevice9** out) {
 if(!p||!out)return D3DERR_INVALIDCALL;
 attachExistingMainWindow();
 logf("D3D9 CreateDevice hook entered focus=%p present_hwnd=%p game_hwnd=%p fps_limit=%d",focus,p->hDeviceWindow,gameWindow,fpsLimit);
 auto fn=(HRESULT(WINAPI*)(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9**))originalMethod(d,16);
 bool ours=gameWindow&&(focus==gameWindow||p->hDeviceWindow==gameWindow);
 bool nativeOutput=needsNativeOutputChain(*p);
 UINT requestedW=p->BackBufferWidth,requestedH=p->BackBufferHeight;BOOL requestedWindowed=p->Windowed;
 if(ours)forcePresentation(*p);HRESULT hr=fn?fn(d,adapter,type,focus,flags,p,out):D3DERR_INVALIDCALL;
 logf("D3D CreateDevice device=%p focus=%p present_hwnd=%p game_hwnd=%p ours=%d flags=%08lx request=%ux%u/%d actual=%ux%u/%d hr=%08lx",SUCCEEDED(hr)&&out?*out:nullptr,focus,p->hDeviceWindow,gameWindow,ours,flags,requestedW,requestedH,requestedWindowed,p->BackBufferWidth,p->BackBufferHeight,p->Windowed,hr);
 if(SUCCEEDED(hr)&&*out){
  if(ours&&(nativeResolution||fpsLimit>0)){hookMethod(*out,17,(void*)platformPresent);logf("D3D9 Present hook installed fps_limit=%d",fpsLimit);}
  if(ours){
   UINT logicalW=requestedW,logicalH=requestedH;if(nativeResolution&&(!logicalW||!logicalH))getNativeDisplaySize(logicalW,logicalH);
   {std::lock_guard<std::mutex> lock(tableMutex);renderDevices.insert(*out);if(nativeResolution)logicalRenderSizes[*out]={logicalW,logicalH};if(nativeOutput)nativeOutputDevices.insert(*out);}
   hookMethod(*out,16,(void*)platformReset);if(nativeResolution&&!scaleToFill){hookMethod(*out,47,(void*)platformSetViewport);hookMethod(*out,64,(void*)platformSetScissorRect);}
   IDirect3DSurface9* back=nullptr;
   if(SUCCEEDED((*out)->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back))){D3DSURFACE_DESC desc={};back->GetDesc(&desc);logf("actual backbuffer %ux%u",desc.Width,desc.Height);back->Release();}
   if(nativeOutput)createNativeOutputChain(*out);
  }
 }
 return hr;
}
static IDirect3D9* WINAPI platformCreateD3D(UINT sdk) {
 initializeAll();logf("Direct3DCreate9 hook entered fps_limit=%d borderless=%d",fpsLimit,windowMode);
 IDirect3D9* d=realCreateD3D?realCreateD3D(sdk):nullptr;
 if(d&&(windowMode||fpsLimit>0))hookMethod(d,16,(void*)platformCreateDevice);return d;
}
static bool d3d9DeviceTable(void** table,uintptr_t moduleBase,uintptr_t moduleEnd,void* present,void* reset) {
 if(!table||table[16]!=reset||table[17]!=present)return false;
 unsigned inImage=0;
 for(size_t i=0;i<119;i++){uintptr_t fn=(uintptr_t)table[i];if(fn>=moduleBase&&fn<moduleEnd)inImage++;}
 return inImage>=100;
}
static unsigned hookExistingD3D9Tables(void* present,void* reset,void** probeTable) {
 HMODULE module=GetModuleHandleA("d3d9.dll");if(!module)return 0;
 BYTE* base=(BYTE*)module;auto dos=(IMAGE_DOS_HEADER*)base;if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return 0;
 auto nt=(IMAGE_NT_HEADERS*)(base+dos->e_lfanew);uintptr_t moduleBase=(uintptr_t)base,moduleEnd=moduleBase+nt->OptionalHeader.SizeOfImage;
 SYSTEM_INFO info={};GetSystemInfo(&info);uintptr_t address=(uintptr_t)info.lpMinimumApplicationAddress,maximum=(uintptr_t)info.lpMaximumApplicationAddress;
 unsigned hooked=0;void* firstTable=nullptr;
 while(address<maximum){
  MEMORY_BASIC_INFORMATION region={};SIZE_T queried=VirtualQuery((void*)address,&region,sizeof(region));if(!queried)break;
  uintptr_t start=(uintptr_t)region.BaseAddress,end=start+region.RegionSize;
  DWORD protection=region.Protect&0xff;
  bool readable=protection==PAGE_READONLY||protection==PAGE_READWRITE||protection==PAGE_WRITECOPY||protection==PAGE_EXECUTE_READ||protection==PAGE_EXECUTE_READWRITE||protection==PAGE_EXECUTE_WRITECOPY;
  if(region.State==MEM_COMMIT&&region.Type==MEM_PRIVATE&&readable&&!(region.Protect&PAGE_GUARD)){
   uintptr_t word=(start+sizeof(void*)-1)&~(uintptr_t)(sizeof(void*)-1);
   for(;word+sizeof(void*)<=end;word+=sizeof(void*)){
    if(*(void**)word!=present||word<start+17*sizeof(void*))continue;
    void** table=(void**)(word-17*sizeof(void*));
    if((uintptr_t)table+119*sizeof(void*)>end)continue;
    if(table==probeTable||!d3d9DeviceTable(table,moduleBase,moduleEnd,present,reset))continue;
    hookTable(table,17,(void*)platformPresent);hookTable(table,16,(void*)platformReset);
    if(!firstTable)firstTable=table;hooked++;
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
 for(unsigned attempt=1;attempt<=120;attempt++){
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
 scaleFilter=std::clamp(opt("scale_filter",2),0,2);
 forceNativeBuffer=(renderWidth!=0||renderHeight!=0);
 if(!renderWidth)renderWidth=monitorBounds.right-monitorBounds.left;if(!renderHeight)renderHeight=monitorBounds.bottom-monitorBounds.top;
 renderWidth=std::max(renderWidth,640);renderHeight=std::max(renderHeight,360);
 char exe[MAX_PATH]={};GetModuleFileNameA(nullptr,exe,MAX_PATH);const char* name=strrchr(exe,'\\');name=name?name+1:exe;
 strcpy_s(gameExeName,name);
 if(!_stricmp(name,"OROCHI_Z_TC.exe"))forceNativeBuffer=true;
 if(imeIsolation){HMODULE imm=systemDll("imm32.dll");if(imm){disableIme=(decltype(disableIme))GetProcAddress(imm,"ImmDisableIME");associateIme=(decltype(associateIme))GetProcAddress(imm,"ImmAssociateContextEx");}
  // Some older Koei titles crash during pre-window startup while third-party
  // IME hooks are active. Disable IME at platform initialization as well as
  // immediately before main-window creation.
  if(disableIme)logf("ImmDisableIME during platform init result=%d",disableIme((DWORD)-1));
 }
 logf("platform hooks=%d mask=%08lx shared=%d borderless=%d render=%dx%d native_render=%d scale_to_fill=%d scale_filter=%u IME=%d COM=%d close=%d fps_limit=%d",earlyHookCount,earlyHookMask,sharedInput,windowMode,renderWidth,renderHeight,nativeResolution,scaleToFill,scaleFilter,imeIsolation,comCompatibility,quickClose,fpsLimit);
 attachExistingMainWindow();
 if(windowMode||fpsLimit>0)attachLateD3DPresentHook();
}
static void patchGraphics() {
 if(!windowMode||graphicsPatched)return;
 // The native resolution constants are specific to the inspected Orochi Z
 // build. Other games use the generic D3D9 CreateDevice/Reset hooks below.
 if(!nativeMenuMap)return;
 BYTE* base=(BYTE*)GetModuleHandleA(nullptr);
 // Verify all getters before modifying any of them.
 const DWORD getters[]={0x75d00,0x75d20,0x75d40,0x75d60};
 for(auto r:getters)if(memcmp(base+r,"\x0f\xb6\x05",3))return;
 const BYTE modeCheck[]={0x83,0xec,0x10,0x55,0x8b,0x6c,0x24,0x1c};
 if(memcmp(base+0x1cafc0,modeCheck,sizeof(modeCheck)))return;
 bool ok=constantGetter(base,0x75d00,renderWidth)&&constantGetter(base,0x75d20,renderHeight)
  &&constantGetter(base,0x75d40,renderHeight)&&constantGetter(base,0x75d60,1);
 // Windowed buffers need not be physical fullscreen display modes. Actual
 // allocation/capability errors still come from Direct3D CreateDevice/Reset.
 const BYTE allowWindowedSize[]={0xb8,1,0,0,0,0xc2,8,0};
 ok=ok&&replaceCode(base+0x1cafc0,modeCheck,sizeof(modeCheck),allowWindowedSize,sizeof(allowWindowedSize));
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
 logApartment("after window");
 if(associateIme)associateIme(h,nullptr,0);
 if(windowMode){
  LONG_PTR style=GetWindowLongPtrA(h,GWL_STYLE);style=(style&~(LONG_PTR)WS_OVERLAPPEDWINDOW)|WS_POPUP;SetWindowLongPtrA(h,GWL_STYLE,style);
  LONG_PTR exStyle=GetWindowLongPtrA(h,GWL_EXSTYLE);exStyle&=~(LONG_PTR)(WS_EX_CLIENTEDGE|WS_EX_WINDOWEDGE|WS_EX_DLGMODALFRAME|WS_EX_TOPMOST);SetWindowLongPtrA(h,GWL_EXSTYLE,exStyle);
  x=monitorBounds.left;y=monitorBounds.top;width=monitorBounds.right-x;height=monitorBounds.bottom-y;
  SetWindowPos(h,HWND_NOTOPMOST,x,y,width,height,SWP_FRAMECHANGED|SWP_NOACTIVATE);
 }
 else SetWindowPos(h,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
 logf("main window hooked hwnd=%p unicode=%d bounds=%d,%d %dx%d",h,unicode,x,y,width,height);
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
static HWND WINAPI platformCreateWindow(DWORD ex,LPCSTR cls,LPCSTR title,DWORD style,int x,int y,int width,int height,HWND parent,HMENU menu,HINSTANCE inst,LPVOID param) {
 initializeAll();bool target=chooseMainWindowA(cls,title,parent);
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
 initializeAll();bool target=chooseMainWindowW(cls,title,parent);
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
  auto names=desc->OriginalFirstThunk?(IMAGE_THUNK_DATA*)(base+desc->OriginalFirstThunk):nullptr;
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
