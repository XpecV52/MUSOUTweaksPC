#ifdef _M_IX86
static constexpr DWORD orochiJoinedRva=0x3fdac8;
static constexpr WORD orochiConfirm=0x80,orochiCancel=0x40;
static constexpr WORD orochiMenuMask=15|orochiConfirm|orochiCancel;
static BYTE* orochiMenuBase;
static int (__cdecl *originalOrochiSelector)(void*,int,int);
static int (__cdecl *originalOrochiPreparation)();
static DWORD (__cdecl *originalOrochiPressed)(int);
static DWORD (__cdecl *originalOrochiRepeated)(int);
static bool orochiSecondJoined=false;
static bool orochiInputLogged=false;

static bool orochiJoined(){
 const bool joined=(orochiMenuBase[orochiJoinedRva]&2)!=0;
 if(joined){orochiSecondJoined=true;InterlockedExchange(&secondMenuKeyboardPassthrough,1);}
 return joined;
}

struct OrochiMenuInput;
static thread_local OrochiMenuInput* activeOrochiMenu;
static thread_local OrochiMenuInput* fallbackOrochiMenu;
struct OrochiMenuInput {
 OrochiMenuInput* previous=activeOrochiMenu;
 LONG previousSplit=InterlockedExchange(&nativeSplitMenuActive,1);
 LONG previousSecond=InterlockedExchange(&nativeSecondMenuActive,0);
 SplitMenuKeys pad,keyboard;
 SplitMenuKeys secondPad;
 bool ownsInput=false;
 bool selectorFirstReady;
 bool selectorFlow;
 ULONGLONG lastPollEpoch=~0ull;

 explicit OrochiMenuInput(bool ready,bool selector=false):selectorFirstReady(ready),selectorFlow(selector){activeOrochiMenu=this;}
 ~OrochiMenuInput(){
  activeOrochiMenu=previous;
  InterlockedExchange(&nativeSplitMenuActive,previousSplit);
  InterlockedExchange(&nativeSecondMenuActive,previousSecond);
 }
 void poll(){
  std::lock_guard<std::recursive_mutex> lock(mutex);
  const auto state=sample();
  if(!armed||!wasConnected||!foreground()){ownsInput=false;return;}
  const auto now=GetTickCount64();
  // Pressed/repeated are queried once for each player. Sampling again for
  // player 2 would turn the same physical edge into an empty edge.
  const ULONGLONG epoch=now/8;
  if(epoch==lastPollEpoch)return;
  lastPollEpoch=epoch;
  const WORD padState=splitPadState(state,(WORD)lastPadButtons,orochiConfirm,orochiCancel);
  const WORD keyboardState=splitKeyboardState(orochiConfirm,orochiCancel);
  if(orochiJoined()||selectorFirstReady)orochiSecondJoined=true;
  const bool secondAllowed=orochiSecondJoined;
  InterlockedExchange(&nativeSecondMenuActive,secondAllowed);
  const auto secondState=sampleSecond();
  secondPad.update(splitSecondPadState(secondState,orochiConfirm,orochiCancel),now);
  if(!ownsInput){
   // A key held when a screen opens must be released before it can confirm.
   pad.held=padState;keyboard.held=keyboardState;
   pad.nextRepeat=keyboard.nextRepeat=now+400;
  }
  ownsInput=true;
  pad.update(padState,now);
  keyboard.update(keyboardState,now);
 }
 DWORD actions(DWORD native,int player,bool repeat,bool firstReady) const {
  if(!ownsInput)return native;
  if(player==0){
   const WORD buttons=repeat?pad.repeated:pad.pressed;
   return (native&~DWORD(orochiMenuMask))|buttons;
  }
  if(player==0)return native;
  const WORD buttons=(repeat?keyboard.repeated:keyboard.pressed)|
                     (repeat?secondPad.repeated:secondPad.pressed);
  // The native join flag is raised after the selector has already entered its
  // player-two loop on some language builds. Keep movement available in that
  // loop and gate only confirm/back until the join/readiness state is real.
  const WORD allowed=(orochiSecondJoined&&((selectorFlow&&orochiJoined())||firstReady||!selectorFlow))?orochiMenuMask:15;
  InterlockedExchange(&nativeSecondMenuActive,allowed==orochiMenuMask);
  return (native&~DWORD(orochiMenuMask))|(buttons&allowed);
 }
};

static OrochiMenuInput* fallbackOrochiInput(){
 if(!menuSceneActive||!controllerTakeover){
  delete fallbackOrochiMenu;fallbackOrochiMenu=nullptr;return nullptr;
 }
 if(!fallbackOrochiMenu)fallbackOrochiMenu=new OrochiMenuInput(false);
 return fallbackOrochiMenu;
}

// The four sequential two-player flows reach these calls after 1P has returned
// success. Other player-2 calls also exist for scenarios without that gate.
static bool orochiSelectorFirstReady(uintptr_t caller){
 switch(caller){
  case 0x145a74:case 0x145bcf:case 0x145cc3:case 0x145df4:return true;
  default:return false;
 }
}
static __declspec(noinline) int __cdecl runOrochiSelector(void* context,int mode,int player){
 const auto caller=(uintptr_t)_ReturnAddress()-(uintptr_t)orochiMenuBase;
 const bool second=player==1&&orochiSelectorFirstReady(caller);
 if(second)orochiSecondJoined=true;
 OrochiMenuInput input(second,true);
 return originalOrochiSelector(context,mode,player);
}
static int __cdecl runOrochiPreparation(){
 OrochiMenuInput input(false);
 return originalOrochiPreparation();
}
static DWORD __cdecl orochiSelectRepeated(int player){
 const DWORD native=originalOrochiRepeated(player);
 auto* input=activeOrochiMenu;
 if(!input)input=fallbackOrochiInput();
 if(!input)return native;
 input->poll();
 if(logging&&!orochiInputLogged){orochiInputLogged=true;logf("Orochi menu input callback repeated player=%d joined=%d",player,orochiSecondJoined?1:0);}
 return input->actions(native,player,true,input->selectorFirstReady);
}
static DWORD __cdecl orochiSelectPressed(int player){
 auto* input=activeOrochiMenu;
 if(!input)input=fallbackOrochiInput();
 if(input)input->poll();
 const DWORD native=originalOrochiPressed(player);
 if(logging&&!orochiInputLogged){orochiInputLogged=true;logf("Orochi menu input callback pressed player=%d joined=%d",player,orochiSecondJoined?1:0);}
 return input?input->actions(native,player,false,input->selectorFirstReady):native;
}
static DWORD __cdecl orochiPrepareRepeated(int player,DWORD firstReady){
 const DWORD native=originalOrochiRepeated(player);
 auto* input=activeOrochiMenu;
 if(!input)return native;
 // Native preparation processes player 0 before player 1 on every frame.
 if(player==0)input->poll();
 return input->actions(native,player,true,firstReady!=0);
}
static DWORD __cdecl orochiPreparePressed(int player,DWORD firstReady){
 auto* input=activeOrochiMenu;
 if(input&&player==0)input->poll();
 const DWORD native=originalOrochiPressed(player);
 return input?input->actions(native,player,false,firstReady!=0):native;
}
// The two native call sites keep the readiness record in EDI. Its fields at
// +0x10 and +0x14 belong to player 1 and player 2, respectively.
static DWORD __declspec(naked) orochiPrepareRepeatHook(){
 __asm {
  pushfd
  push ecx
  push edx
  push dword ptr [edi+0x10]
  push dword ptr [esp+20]
  call orochiPrepareRepeated
  add esp,8
  pop edx
  pop ecx
  popfd
  ret
 }
}
static DWORD __declspec(naked) orochiPreparePressHook(){
 __asm {
  pushfd
  push ecx
  push edx
  push dword ptr [edi+0x10]
  push dword ptr [esp+20]
  call orochiPreparePressed
  add esp,8
  pop edx
  pop ecx
  popfd
  ret
 }
}
static DWORD __cdecl orochiJoinAcknowledgement(int player){
 const DWORD native=originalOrochiPressed(player);
 auto* input=activeOrochiMenu;
 if(!input||player==0)return native;
 // The stock join dialog waits for confirmation from the joining device.
 // Under the split menu policy this is still a public prompt owned by 1P.
 input->poll();
 return input->ownsInput?input->pad.pressed&orochiConfirm:native;
}

static bool installOrochiSplitMenuRouting(BYTE* base,DWORD imageSize){
 constexpr DWORD selector=0x1444d0,preparation=0x135230;
 constexpr DWORD pressed=0x1ae1b0,repeated=0x1ae1f0;
 const BYTE selectorPrefix[]={0x83,0xec,0x20,0x56,0x8b,0x74,0x24,0x30};
 const BYTE preparationPrefix[]={0x55,0x8b,0xec,0x83,0xe4,0xf8};
 if(imageSize<=orochiJoinedRva||memcmp(base+selector,selectorPrefix,sizeof(selectorPrefix))||
    memcmp(base+preparation,preparationPrefix,sizeof(preparationPrefix)))return false;
 struct CallPatch {DWORD site,target;const void* hook;BYTE original[5],replacement[5];};
 CallPatch calls[]={
  {0x14479a,repeated,(const void*)orochiSelectRepeated},
  {0x1447a7,pressed,(const void*)orochiSelectPressed},
  {0x13541e,repeated,(const void*)orochiPrepareRepeatHook},
  {0x13542d,pressed,(const void*)orochiPreparePressHook},
  {0x14e7c1,pressed,(const void*)orochiJoinAcknowledgement},
  {0x135bf4,preparation,(const void*)runOrochiPreparation},
  {0x1459e1,selector,(const void*)runOrochiSelector},
  {0x145a6f,selector,(const void*)runOrochiSelector},
  {0x145ab2,selector,(const void*)runOrochiSelector},
  {0x145b03,selector,(const void*)runOrochiSelector},
  {0x145b85,selector,(const void*)runOrochiSelector},
  {0x145bca,selector,(const void*)runOrochiSelector},
  {0x145c29,selector,(const void*)runOrochiSelector},
  {0x145cbe,selector,(const void*)runOrochiSelector},
  {0x145cf5,selector,(const void*)runOrochiSelector},
  {0x145d55,selector,(const void*)runOrochiSelector},
  {0x145db5,selector,(const void*)runOrochiSelector},
  {0x145def,selector,(const void*)runOrochiSelector},
  {0x145e6d,selector,(const void*)runOrochiSelector}
 };
 for(auto& call:calls){
  call.original[0]=call.replacement[0]=0xe8;
  const DWORD oldOffset=call.target-call.site-5;
  const DWORD newOffset=(DWORD)((uintptr_t)call.hook-(uintptr_t)(base+call.site+5));
  memcpy(call.original+1,&oldOffset,4);
  memcpy(call.replacement+1,&newOffset,4);
  if(memcmp(base+call.site,call.original,sizeof(call.original)))return false;
 }
 orochiMenuBase=base;
 originalOrochiSelector=(decltype(originalOrochiSelector))(base+selector);
 originalOrochiPreparation=(decltype(originalOrochiPreparation))(base+preparation);
 originalOrochiPressed=(decltype(originalOrochiPressed))(base+pressed);
 originalOrochiRepeated=(decltype(originalOrochiRepeated))(base+repeated);
 for(auto& call:calls){
  if(replaceCode(base+call.site,call.original,5,call.replacement,5))continue;
  for(auto* applied=calls;applied!=&call;++applied)
   replaceCode(base+applied->site,applied->replacement,5,applied->original,5);
  return false;
 }
 // The camp, equipment and results pages call the same input helpers from
 // additional functions. Route every remaining direct call so those pages
 // receive the same split policy as character selection and preparation.
 auto* dos=(IMAGE_DOS_HEADER*)base;
 auto* nt=(IMAGE_NT_HEADERS*)(base+dos->e_lfanew);
 auto patchRemaining=[&](DWORD target,const void* hook){
  unsigned patched=0;
  auto* sections=IMAGE_FIRST_SECTION(nt);
  for(unsigned s=0;s<nt->FileHeader.NumberOfSections;s++,sections++){
   // Orochi's custom RIF sections omit IMAGE_SCN_MEM_EXECUTE in the PE
   // header even though the loader maps them as code. The known menu calls
   // live in those sections, so filtering by the header flag misses them.
   BYTE* start=base+sections->VirtualAddress;
   const DWORD size=sections->Misc.VirtualSize;
   for(DWORD off=0;off+5<=size;off++){
    BYTE* site=start+off;if(site[0]!=0xe8)continue;
    // The call displacement is signed. Doing pointer arithmetic with an
    // unsigned DWORD makes negative displacements fail to match on MSVC,
    // leaving child-menu call sites on the native shared input path.
    const LONG rel=*(const LONG*)(site+1);
    const uintptr_t destination=(uintptr_t)(site+5)+(intptr_t)rel;
    if(destination!=(uintptr_t)(base+target))continue;
    BYTE original[5],replacement[5]={0xe8};memcpy(original,site,5);
    const DWORD next=(DWORD)(uintptr_t)(site+5);
    const DWORD hookRel=(DWORD)((uintptr_t)hook-next);memcpy(replacement+1,&hookRel,4);
    if(replaceCode(site,original,5,replacement,5))patched++;
   }
  }
  return patched;
 };
 const unsigned pressedExtra=patchRemaining(pressed,(const void*)orochiSelectPressed);
 const unsigned repeatedExtra=patchRemaining(repeated,(const void*)orochiSelectRepeated);
 logf("Orochi Z split menu routing installed for character selection, preparation and child menus; extra pressed=%u repeated=%u",pressedExtra,repeatedExtra);
 return true;
}
#else
static bool installOrochiSplitMenuRouting(BYTE*,DWORD){return false;}
#endif
