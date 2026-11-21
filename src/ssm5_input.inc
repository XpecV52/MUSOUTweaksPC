#ifdef _M_IX86
static constexpr DWORD ssm5PollRva=0x218190;
static constexpr DWORD ssm5MapsRva=0x367f60;
static constexpr DWORD ssm5MapModeRva=0x35d670;
static constexpr DWORD ssm5PlayerStateRva=0x35cf40;
static constexpr DWORD ssm5PlayerStride=0x140;
static constexpr WORD ssm5Confirm=0x8,ssm5Cancel=0x4;
static constexpr DWORD ssm5MenuMask=0x780c;
static constexpr DWORD ssm5Join=0x100;
static constexpr WORD ssm5InputConfirm=0x0200,ssm5InputCancel=0x0100;
static constexpr WORD ssm5InputX=0x0400,ssm5InputY=0x0800;
static BYTE* ssm5InputBase;
static void (__cdecl *originalSsm5Poll)();
static void (__stdcall *originalSsm5Character)(const BYTE*);
static int (__stdcall *originalSsm5Preparation)(const BYTE*);
static SplitMenuKeys ssm5PadKeys,ssm5KeyboardKeys;
static bool ssm5MenuOwned;
static int ssm5LastMenu=-1,ssm5LastJoined=-1,ssm5LastActive=-1;
static unsigned ssm5PollCalls=0,ssm5PreparationCalls=0;
static bool ssm5BindingLogged=false;
static bool ssm5DirectionsUse(const NativeMap& map,int device){
 if(map.count!=20||!writableMemory(map.actions,4*sizeof(NativeAction)))return false;
 for(unsigned i=0;i<4;i++){
  bool found=false;
  for(const auto& binding:map.actions[i].binding)if(binding.device==device){found=true;break;}
  if(!found)return false;
 }
 return true;
}

static DWORD ssm5MenuBits(WORD keys){
 // Keep the shared direction bits distinct from SM5's confirm/back bits.
 return (DWORD(keys&15)<<11)|((keys&ssm5InputCancel)?ssm5Cancel:0)|((keys&ssm5InputConfirm)?ssm5Confirm:0);
}
static WORD ssm5PadMenuState(const PadState& state){
 WORD keys=splitPadState(state,(WORD)lastPadButtons,ssm5InputConfirm,ssm5InputCancel);
 if(state.buttons[2])keys|=ssm5InputX;
 if(state.buttons[3])keys|=ssm5InputY;
 return keys;
}
static DWORD ssm5MenuActionBits(WORD keys){
 return ssm5MenuBits(keys)|(DWORD(keys)&(ssm5InputX|ssm5InputY));
}
static void writeSsm5MenuInput(unsigned player,const SplitMenuKeys& keys,DWORD mask,bool preserveExtras=true){
 auto* record=(DWORD*)(ssm5InputBase+ssm5PlayerStateRva+player*ssm5PlayerStride);
 const WORD held=(keys.held&~15)|(keys.repeated&15);
 const DWORD extras=preserveExtras?~mask:0;
 record[2]=(record[2]&extras)|(ssm5MenuActionBits(held)&mask);
 record[6]=(record[6]&extras)|(ssm5MenuActionBits(keys.pressed)&mask);
 record[8]=(record[8]&extras)|(ssm5MenuActionBits(keys.repeated)&mask);
}

static void __cdecl readSsm5Inputs(){
 std::lock_guard<std::recursive_mutex> lock(mutex);
 if(logging&&ssm5PollCalls<5)logf("SM5 native poll call=%u",++ssm5PollCalls);
 const auto state=sample();
 const bool own=controllerTakeover&&armed&&wasConnected&&foreground();
 const auto* gameMode=(const DWORD*)(ssm5InputBase+ssm5MapModeRva);
 // SM5 keeps the option/2P setup pages in the same native mode records as
 // the selector, so the two mode words are not a reliable menu test. The
 // renderer's menu state covers those child pages while the old mode check
 // remains useful during startup before D3D has reported a scene.
 const bool menu=own&&(menuSceneActive||(!gameMode[0]&&!gameMode[1]));
 auto* maps=(NativeMap*)(ssm5InputBase+ssm5MapsRva);
 if(logging&&!ssm5BindingLogged){
  ssm5BindingLogged=true;
  for(unsigned player=0;player<2;player++)for(unsigned action=0;action<20;action++)
   logf("SM5 binding p=%u a=%u b0=%d/%u/%d/%d b1=%d/%u/%d/%d",player,action,
        maps[player].actions[action].binding[0].device,maps[player].actions[action].binding[0].axis,
        maps[player].actions[action].binding[0].sign,maps[player].actions[action].binding[0].object,
        maps[player].actions[action].binding[1].device,maps[player].actions[action].binding[1].axis,
        maps[player].actions[action].binding[1].sign,maps[player].actions[action].binding[1].object);
 }
 const auto* battleFlags=*(const DWORD* const*)(ssm5InputBase+0x35d53c);
 const bool joined=battleFlags&&(*battleFlags&1);
 if(logging&&(ssm5LastMenu!=(menu?1:0)||ssm5LastJoined!=(joined?1:0))){
  ssm5LastMenu=menu?1:0;ssm5LastJoined=joined?1:0;
  logf("SM5 input state menu=%d joined=%d mode=%08lx/%08lx maps=%p/%p counts=%lu/%lu",
       ssm5LastMenu,ssm5LastJoined,gameMode[0],gameMode[1],maps[0].actions,maps[1].actions,maps[0].count,maps[1].count);
 }
 // The 2P settings page and its child pages have no joined-player flag even
 // though they still read the second action record. Keep menu availability
 // separate from the native joined flag. The latter only gates the native
 // 2P cursor and confirmation state.
 InterlockedExchange(&nativeSplitMenuActive,menu);
 InterlockedExchange(&nativeSecondMenuActive,menu);
 const bool swapPlayers=own&&joined&&gameMode[0]&&gameMode[1]&&
                        ssm5DirectionsUse(maps[0],8)&&ssm5DirectionsUse(maps[1],0);
 if(swapPlayers)std::swap(maps[0],maps[1]);
 NativeAction firstPlayerActions[20],secondPlayerActions[20];
 NativeAction* firstOriginal=nullptr;
 NativeAction* secondOriginal=nullptr;
 if(own&&joined&&gameMode[0]&&maps[0].useDevice&&maps[0].device<8&&maps[0].count==20&&
    writableMemory(maps[0].actions,sizeof(firstPlayerActions))){
  // Both bindings of an action are ORed. Keyboard bindings in the 1P
  // map also fire on 2P attacks or moves whenever physical keys overlap.
  firstOriginal=maps[0].actions;
  memcpy(firstPlayerActions,firstOriginal,sizeof(firstPlayerActions));
  for(auto& action:firstPlayerActions)for(auto& binding:action.binding)
   if(binding.device==8)binding.device=10;
  maps[0].actions=firstPlayerActions;
 }
 if(firstOriginal&&gameMode[1]&&maps[1].count==20&&writableMemory(maps[1].actions,sizeof(secondPlayerActions))){
  secondOriginal=maps[1].actions;
  memcpy(secondPlayerActions,secondOriginal,sizeof(secondPlayerActions));
  for(auto& action:secondPlayerActions)for(auto& binding:action.binding)
   if(binding.device>=0&&binding.device<8)binding.device=10;
  maps[1].actions=secondPlayerActions;
 }
 originalSsm5Poll();
 if(secondOriginal)maps[1].actions=secondOriginal;
 if(firstOriginal)maps[0].actions=firstOriginal;
 if(swapPlayers)std::swap(maps[0],maps[1]);

 if(!menu){ssm5MenuOwned=false;ssm5PadKeys={};ssm5KeyboardKeys={};InterlockedExchange(&nativeSecondMenuActive,0);return;}
 const auto now=GetTickCount64();
 const WORD pad=ssm5PadMenuState(state);
 const auto secondPad=sampleSecond();
 const WORD keyboard=(WORD)(splitKeyboardState(ssm5InputConfirm,ssm5InputCancel)|
                            splitSecondPadState(secondPad,ssm5InputConfirm,ssm5InputCancel));
 if(!ssm5MenuOwned){
  ssm5PadKeys.held=pad;ssm5KeyboardKeys.held=keyboard;
  ssm5PadKeys.nextRepeat=ssm5KeyboardKeys.nextRepeat=now+400;
 }
 ssm5MenuOwned=true;
 ssm5PadKeys.update(pad,now);
 ssm5KeyboardKeys.update(keyboard,now);
 // Public menus belong to 1P's controller. Do not let the shared keyboard
 // map's TAB/J/I/Space/Shift actions leak back into 1P.
 writeSsm5MenuInput(0,ssm5PadKeys,ssm5MenuMask|ssm5InputX|ssm5InputY,false);
 if(menu)writeSsm5MenuInput(1,ssm5KeyboardKeys,ssm5MenuMask,true);
 // Preserve only the native join shortcut until a native 2P consumer has
 // confirmed that 2P has joined and owns the current cursor.
 auto* second=(DWORD*)(ssm5InputBase+ssm5PlayerStateRva+ssm5PlayerStride);
 for(unsigned field:{2u,6u,8u}){
  // Keep the second source available on setup and child pages even before
  // the native join flag is raised. The native poll's other action bits carry
  // TAB/J/I/Space/Shift and this replaces only the shared menu controls.
  if(!menu)second[field]&=ssm5Join;
 }
}

static void primeSsm5MenuInput(){
 if(ssm5MenuOwned)return;
 std::lock_guard<std::recursive_mutex> lock(mutex);
 const auto now=GetTickCount64();
 const auto state=sample();
 const auto secondState=sampleSecond();
 const WORD pad=ssm5PadMenuState(state);
 const WORD keyboard=(WORD)(splitKeyboardState(ssm5InputConfirm,ssm5InputCancel)|
                            splitSecondPadState(secondState,ssm5InputConfirm,ssm5InputCancel));
 ssm5PadKeys={};ssm5KeyboardKeys={};
 ssm5PadKeys.held=pad;ssm5KeyboardKeys.held=keyboard;
 ssm5PadKeys.nextRepeat=ssm5KeyboardKeys.nextRepeat=now+400;
 ssm5MenuOwned=true;
}

static void __stdcall runSsm5Character(const BYTE* screen){
 // The Musou selector stores 1P's chosen character before setting its
 // cursor index to 1. The roster uses 0x29 as its empty selection.
 const bool second=ssm5MenuOwned&&*(const DWORD*)(screen+0x30)==1&&*(const DWORD*)(screen+0x18)<0x29;
 if(logging)logf("SM5 character screen cursor=%lu index=%lu second=%d",*(const DWORD*)(screen+0x30),*(const DWORD*)(screen+0x18),second?1:0);
 InterlockedExchange(&nativeSecondMenuActive,second);
 if(second)writeSsm5MenuInput(1,ssm5KeyboardKeys,ssm5MenuMask,true);
 originalSsm5Character(screen);
 InterlockedExchange(&nativeSecondMenuActive,0);
}

static int __stdcall runSsm5Preparation(const BYTE* screen){
 // The preparation cursor loop counts its players with +8 and tests the
 // per-player ready flags at +0x48/49. Its subpages use the active player.
 if(!screen){InterlockedExchange(&nativeSecondMenuActive,0);return originalSsm5Preparation(screen);}
 if(logging&&ssm5PreparationCalls<8)logf("SM5 preparation callback=%u",++ssm5PreparationCalls);
 primeSsm5MenuInput();
 const auto* battleFlags=*(const DWORD* const*)(ssm5InputBase+0x35d53c);
 const bool joined=battleFlags&&(*battleFlags&1);
 const DWORD activePlayer=*(const DWORD*)(screen+0x50);
 // The native routine is entered for the whole preparation page. Its active
 // cursor field is transient and is zero while the page switches from 1P to
 // 2P, so do not use it as the gate for the second action record. Keep 2P
 // movement and TAB/J/I/Space/Shift available for the whole joined page.
 const bool secondActive=joined||activePlayer==1;
 const bool firstReady=screen[0x48]!=0;
 const DWORD mask=(joined&&firstReady)?ssm5MenuMask:ssm5MenuMask&~DWORD(ssm5Confirm|ssm5Cancel);
 if(logging&&ssm5LastActive!=static_cast<int>(activePlayer|((secondActive?1:0)<<8))){
  ssm5LastActive=activePlayer|((secondActive?1:0)<<8);
  logf("SM5 preparation active=%lu joined=%d second=%d screen48=%u records=%08lx/%08lx/%08lx/%08lx",
       activePlayer,joined?1:0,secondActive?1:0,screen[0x48],
       *(DWORD*)(ssm5InputBase+ssm5PlayerStateRva+0x08),
       *(DWORD*)(ssm5InputBase+ssm5PlayerStateRva+ssm5PlayerStride+0x08),
       *(DWORD*)(ssm5InputBase+ssm5PlayerStateRva+0x18),
       *(DWORD*)(ssm5InputBase+ssm5PlayerStateRva+ssm5PlayerStride+0x18));
 }
 InterlockedExchange(&nativeSecondMenuActive,secondActive);
 // The native loop shares keyboard readiness with both players. These
 // action words now have independent sources, so scope that flag to the call.
 auto& sharedKeyboard=*(DWORD*)(ssm5InputBase+0x35d6a4);
 const DWORD previousSharedKeyboard=sharedKeyboard;
 const auto state=sample();
 sharedKeyboard=secondActive?ssm5MenuBits(ssm5KeyboardKeys.held):0;
 // Both cursors remain live on the preparation page. The old branch cleared
 // player 1 whenever the second cursor was active, which made both players
 // appear frozen while the native routine was switching focus.
 writeSsm5MenuInput(0,ssm5PadKeys,ssm5MenuMask|ssm5InputX|ssm5InputY,false);
 if(secondActive)writeSsm5MenuInput(1,ssm5KeyboardKeys,mask,true);
 const int result=originalSsm5Preparation(screen);
 sharedKeyboard=previousSharedKeyboard;
 InterlockedExchange(&nativeSecondMenuActive,0);
 return result;
}

static bool installSsm5InputRouting(BYTE* base,DWORD imageSize){
 const DWORD character=0x1a8b50;
 const DWORD preparation=0x1fa400;
 const BYTE pollBytes[]={0x83,0xec,0x14,0x53,0x55,0x33,0xdb};
 const BYTE charBytes[]={0x83,0xec,0x1c,0x53,0x55,0x8b,0x6c,0x24,0x28};
 const BYTE prepBytes[]={0x53,0x55,0x8b,0x6c,0x24,0x0c};
 if(imageSize<ssm5MapsRva+3*sizeof(NativeMap)||memcmp(base+ssm5PollRva,pollBytes,sizeof(pollBytes))||
    memcmp(base+character,charBytes,sizeof(charBytes))||memcmp(base+preparation,prepBytes,sizeof(prepBytes)))return false;
 ssm5InputBase=base;
 if(!buildInlineTrampoline(base+ssm5PollRva,pollBytes,sizeof(pollBytes),(void*)readSsm5Inputs,(void**)&originalSsm5Poll))return false;
 const bool selector=buildInlineTrampoline(base+character,charBytes,sizeof(charBytes),(void*)runSsm5Character,(void**)&originalSsm5Character);
 const bool prep=buildInlineTrampoline(base+preparation,prepBytes,sizeof(prepBytes),(void*)runSsm5Preparation,(void**)&originalSsm5Preparation);
 logf("SSM5 native input routing installed; character selection=%d preparation=%d",selector,prep);
 return true;
}
#endif
