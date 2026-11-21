// The old Koei ports copy the common menu map into both players' action words.
// Route the controls after that copy, before the menu reads either player.
struct SplitMenuProfile {
 DWORD poll, prepareCall, prepare, prepareScreenCall, prepareScreen;
 DWORD joinedFlags, player1Actions, player2Actions;
 DWORD nativeMode, playerMaps, characterJoinPrompt;
 WORD confirm, cancel;
 bool packedActions;
};
static constexpr SplitMenuProfile splitSsm3={
 0x1566f0,0x4b028,0x4a540,0x4b3bc,0x4afa0,0x49e8a8,0x6398e0,0x6398e0,0x32c8c0,0x639d00,0x48f980,0x200,0x100,true
};
static constexpr SplitMenuProfile splitSsm4={
 0x1c7490,0x5d51c,0x5c370,0x5d7fd,0x5d4a0,0x35dc40,0x58da30,0x58db18,0x2e07a0,0x2e07a4,0x3bd728,0x80,0x40,false
};
static const SplitMenuProfile* splitProfile;
static BYTE* splitBase;
static void (__cdecl *originalSplitPoll)();
static int (__cdecl *originalSplitPrepareScreen)(int);
static void* originalSplitPrepare;
static thread_local const DWORD* splitPrepareReadyBits;
static thread_local bool insideSplitPrepareScreen;
static thread_local bool splitCharP1Ready;
static thread_local int splitCharMode=-1;
static bool splitSecondJoined=false;
static BYTE* splitJoinedBase=nullptr;
static int (__cdecl *originalSsm3CharScreen)(int,int,int,int,int);
static int (__cdecl *originalSsm4CharScreen)(int,int,int,int);
static int (__cdecl *originalCharPlayer)(int);

struct SplitMenuKeys {
 WORD held=0, pressed=0, repeated=0;
 ULONGLONG nextRepeat=0;
 void update(WORD next,ULONGLONG now){
  pressed=next&~held;
  repeated=pressed;
  if((next&15)!=(held&15))nextRepeat=now+400;
  else if((next&15)&&now>=nextRepeat){repeated|=next&15;nextRepeat=now+110;}
  held=next;
 }
};
static SplitMenuKeys splitPadKeys,splitKeyboardKeys;
static bool splitSourcesReady=false;
static bool splitMapFingerprintLogged=false;
static int splitLastJoined=-1,splitLastMenu=-1;

static int __cdecl runSplitPrepareScreen(int mode){
 const auto previousReady=splitPrepareReadyBits;
 const bool previousInside=insideSplitPrepareScreen;
 splitPrepareReadyBits=nullptr;
 insideSplitPrepareScreen=true;
 const int result=originalSplitPrepareScreen(mode);
 splitPrepareReadyBits=previousReady;
 insideSplitPrepareScreen=previousInside;
 return result;
}
static int __cdecl runSsm3CharScreen(int first,int second,int mode,int fourth,int fifth){
 const int previous=splitCharMode;
 splitCharMode=mode;
 const int result=originalSsm3CharScreen(first,second,mode,fourth,fifth);
 splitCharMode=previous;
 return result;
}
static int __cdecl runSsm4CharScreen(int first,int mode,int third,int fourth){
 const int previous=splitCharMode;
 splitCharMode=mode;
 const int result=originalSsm4CharScreen(first,mode,third,fourth);
 splitCharMode=previous;
 return result;
}
static bool nativeSplitJoined(){
 if(!splitProfile)return false;
 if(splitJoinedBase!=splitBase){splitJoinedBase=splitBase;splitSecondJoined=false;}
 if(splitCharP1Ready){splitSecondJoined=true;return true;}
 bool live=false;
 if(splitCharMode==2){
  // Same gate the native selector uses before entering the P2 loop. In
  // particular SSM3 needs the unfiltered F1 action until the prompt joins P2.
  const DWORD* join=(const DWORD*)(splitBase+splitProfile->characterJoinPrompt);
  live=join[0]==0||join[1]!=0;
 }else{
  live=(*(const DWORD*)(splitBase+splitProfile->joinedFlags)&0x100)!=0;
 }
 if(live){splitSecondJoined=true;InterlockedExchange(&secondMenuKeyboardPassthrough,1);}
 return live||splitSecondJoined;
}
static bool nativeCommonMenu(){
 if(!splitProfile)return false;
 if(!splitProfile->packedActions)return *(const DWORD*)(splitBase+splitProfile->nativeMode)==0;
 auto game=*(BYTE**)(splitBase+splitProfile->nativeMode);
 return game&&game[0x3f9]==0;
}
static WORD splitKeyboardState(WORD confirm,WORD cancel){
 const int directions[]={'W','S','A','D'};
 WORD state=0;
 for(unsigned i=0;i<4;i++)if(GetAsyncKeyState(directions[i])&0x8000)state|=1<<i;
 // Message-based public menus can still have a 1P-generated key down on
 // entry. Deliver physical aliases here without forwarding that pulse to 2P.
 if((GetAsyncKeyState('L')&0x8000)||((menuKeyMode==1||!menuKeys[0])&&(GetAsyncKeyState(VK_RETURN)&0x8000)))state|=confirm;
 if((GetAsyncKeyState('K')&0x8000)||((menuKeyMode==1||!menuKeys[1])&&(GetAsyncKeyState(VK_ESCAPE)&0x8000)))state|=cancel;
 return state;
}
static WORD splitPadState(const PadState& state,WORD raw,WORD confirm,WORD cancel){
 WORD buttons=(raw&15)|((raw&XINPUT_GAMEPAD_B)?confirm:0)|((raw&XINPUT_GAMEPAD_A)?cancel:0);
 if(state.y<-96)buttons|=1;
 if(state.y>96)buttons|=2;
 if(state.x<-96)buttons|=4;
 if(state.x>96)buttons|=8;
 return buttons;
}
static WORD splitSecondPadState(const PadState& state,WORD confirm,WORD cancel){
 WORD buttons=state.dpad;
 if(state.buttons[1])buttons|=confirm;
 if(state.buttons[0])buttons|=cancel;
 if(state.y<-96)buttons|=1;
 if(state.y>96)buttons|=2;
 if(state.x<-96)buttons|=4;
 if(state.x>96)buttons|=8;
 return buttons;
}
static WORD splitBoundMenuAction(const NativeMap& map,const WORD* actions,unsigned count,int scanCode){
 if(map.count!=count||!writableMemory(map.actions,count*sizeof(NativeAction)))return 0;
 WORD result=0;
 for(unsigned i=0;i<count;i++)for(const auto& binding:map.actions[i].binding){
  if(binding.device==8&&binding.object==scanCode)result|=actions[i];
 }
 return result;
}
static WORD splitLegacyMenuExtras(const NativeMap& keyboard,bool sm3){
 // These are the action-index translations used by the original menu poll
 // when it reads controller bindings from each player's battle map.
 static constexpr WORD sm3Actions[]={1,2,4,8,0x30,0,0,0,0x100,0x200,0,0x800,0,0,0x4000,0x8000};
 static constexpr WORD sm4Actions[]={1,2,4,8,0x30,0,0x40,0x80,0,0x200,0x400,0x800,0x1000,0x2000,0,0};
 const WORD* actions=sm3?sm3Actions:sm4Actions;
 WORD result=0;
 if(GetAsyncKeyState(VK_TAB)&0x8000)result|=sm3?0x800:0x200;
 if(GetAsyncKeyState(VK_SPACE)&0x8000)result|=0x10;
 if(GetAsyncKeyState('J')&0x8000)result|=splitBoundMenuAction(keyboard,actions,16,DIK_J);
 if(GetAsyncKeyState('I')&0x8000)result|=splitBoundMenuAction(keyboard,actions,16,DIK_I);
 if(GetAsyncKeyState(VK_LSHIFT)&0x8000)result|=splitBoundMenuAction(keyboard,actions,16,DIK_LSHIFT);
 if(GetAsyncKeyState(VK_RSHIFT)&0x8000)result|=splitBoundMenuAction(keyboard,actions,16,DIK_RSHIFT);
 return result;
}
static bool splitMenuExtraScanCode(int scanCode){
 return scanCode==DIK_TAB||scanCode==DIK_SPACE||scanCode==DIK_J||scanCode==DIK_I||
        scanCode==DIK_LSHIFT||scanCode==DIK_RSHIFT;
}
static WORD splitKeyboardActions(WORD actions,WORD gated,bool secondCursor,bool ready){
 if(!secondCursor)return 0;
 return ready?actions:actions&~gated;
}
static bool nativeDirectionsUse(const NativeMap& map,int device){
 if(map.count!=16||!writableMemory(map.actions,4*sizeof(NativeAction)))return false;
 for(unsigned i=0;i<4;i++){
  bool found=false;
  for(const auto& binding:map.actions[i].binding)if(binding.device==device){found=true;break;}
  if(!found)return false;
 }
 return true;
}
static void replaceSplitActionWord(unsigned player,unsigned kind,WORD mask,WORD value){
 const auto& profile=*splitProfile;
 DWORD* word=(DWORD*)(splitBase+(player?profile.player2Actions:profile.player1Actions));
 // SSM3 packs both players in one DWORD; SSM4 spaces action words around
 // the previous-frame word, and uses separate structures for the players.
 const unsigned offset=profile.packedActions?kind:(kind?kind+1:0);
 const unsigned shift=profile.packedActions?player*16:0;
 word[offset]=(word[offset]&~(DWORD(mask)<<shift))|(DWORD(value&mask)<<shift);
}
static int __cdecl runSplitCharPlayer(int player){
 const bool previous=splitCharP1Ready;
 // Mode 1 opens 2P with 1P's existing selection. Mode 2 enters the
 // same player loop after 1P confirms and the native join check passes.
 splitCharP1Ready=(splitCharMode==1||splitCharMode==2)&&player==1;
 if(splitCharP1Ready)splitSecondJoined=true;
 if(splitCharP1Ready){
  std::lock_guard<std::recursive_mutex> lock(mutex);
  // The selector can read the previous frame before polling 2P input.
  if(controllerTakeover&&armed&&wasConnected&&foreground()){
   for(unsigned kind=0;kind<3;kind++)replaceSplitActionWord(1,kind,0xffff,0);
  }
 }
 const int result=originalCharPlayer(player);
 splitCharP1Ready=previous;
 return result;
}
static void applySplitMenuActions(bool secondCursor, bool firstReady){
 // 1P owns public menus as soon as the controller is armed. The native
 // joined flag only gates the separate 2P cursor and its confirm/back keys.
 if(!splitSourcesReady)return;
 const auto& profile=*splitProfile;
 const WORD buttons=profile.confirm|profile.cancel;
 const WORD gated=buttons|0x30;
 const WORD firstMask=15|buttons;
 // Preserve 1P's other native menu buttons. The shared map contributes no
 // actions to 2P, whose cursor and confirmation use only physical keyboard.
 replaceSplitActionWord(0,0,firstMask,splitPadKeys.held);
 replaceSplitActionWord(0,1,firstMask,splitPadKeys.pressed);
 replaceSplitActionWord(0,2,firstMask,splitPadKeys.repeated);
 const bool secondEnabled=secondCursor&&nativeSplitJoined();
 if(secondEnabled){
  replaceSplitActionWord(1,0,0xffff,splitKeyboardActions(splitKeyboardKeys.held,gated,true,firstReady));
  replaceSplitActionWord(1,1,0xffff,splitKeyboardActions(splitKeyboardKeys.pressed,gated,true,firstReady));
  replaceSplitActionWord(1,2,0xffff,splitKeyboardActions(splitKeyboardKeys.repeated,gated,true,firstReady));
 }
}
static void __cdecl readSplitMenuInputs(){
 std::lock_guard<std::recursive_mutex> lock(mutex);
 NativeMap* maps=(NativeMap*)(splitBase+splitProfile->playerMaps);
 const bool haveOwner=controllerTakeover&&armed&&wasConnected&&foreground();
 // The legacy mode word is zero in both the title menu and short battle
 // transitions on some SM4 builds. Require the renderer's menu scene bit as
 // a second signal so a battle frame cannot be routed as a public menu.
 const bool menu=insideSplitPrepareScreen||splitCharMode>=0||
                 (nativeCommonMenu()&&menuSceneActive);
 const bool secondCursor=(insideSplitPrepareScreen||splitCharP1Ready)&&nativeSplitJoined();
 InterlockedExchange(&nativeSplitMenuActive,haveOwner&&menu);
 InterlockedExchange(&nativeSecondMenuActive,haveOwner&&menu&&secondCursor);
 const bool reversed=nativeDirectionsUse(maps[0],8)&&nativeDirectionsUse(maps[1],0);
 if(logging&&!splitMapFingerprintLogged){
  splitMapFingerprintLogged=true;
  logf("split map fingerprint maps=%p/%p count=%lu/%lu d00=%d/%d d01=%d/%d d10=%d/%d d11=%d/%d joined=%d menu=%d reversed=%d",
       maps[0].actions,maps[1].actions,maps[0].count,maps[1].count,
       maps[0].actions?maps[0].actions[0].binding[0].device:-1,maps[0].actions?maps[0].actions[0].binding[0].object:-1,
       maps[0].actions?maps[0].actions[1].binding[0].device:-1,maps[0].actions?maps[0].actions[1].binding[0].object:-1,
       maps[1].actions?maps[1].actions[0].binding[0].device:-1,maps[1].actions?maps[1].actions[0].binding[0].object:-1,
       maps[1].actions?maps[1].actions[1].binding[0].device:-1,maps[1].actions?maps[1].actions[1].binding[0].object:-1,
       nativeSplitJoined()?1:0,menu?1:0,reversed?1:0);
 }
 // The native mode words are transient and are zero during some battle
 // transitions. The player maps themselves are the stable role fingerprint,
 // so do not gate the handoff on those words or P2 can lose all attacks for a
 // frame or for an entire stage.
 const bool swapPlayers=haveOwner&&!menu&&reversed;
 NativeAction firstBattleActions[16],secondBattleActions[16];
 NativeAction* firstBattleOriginal=nullptr;
 NativeAction* secondBattleOriginal=nullptr;
 auto& commonMap=maps[2];
 NativeAction commonActions[16];
 NativeAction* commonOriginal=nullptr;
 if(haveOwner&&menu&&secondCursor&&commonMap.count==16&&writableMemory(commonMap.actions,sizeof(commonActions))){
  // Keep native controller bindings available to 1P while 2P uses the
  // physical extras on their own character/preparation page.
  commonOriginal=commonMap.actions;
  memcpy(commonActions,commonOriginal,sizeof(commonActions));
  for(auto& action:commonActions)for(auto& binding:action.binding){
   if(binding.device==8&&splitMenuExtraScanCode(binding.object))binding.device=10;
  }
  commonMap.actions=commonActions;
 }
 if(swapPlayers)std::swap(maps[0],maps[1]);
 if(haveOwner&&!menu){
  // The native maps can contain two bindings per action. After the player
  // roles are swapped, remove the opposite device from each temporary map so
  // 2P keyboard actions cannot also fire 1P and vice versa.
  if(maps[0].count==16&&writableMemory(maps[0].actions,sizeof(firstBattleActions))){
   firstBattleOriginal=maps[0].actions;memcpy(firstBattleActions,firstBattleOriginal,sizeof(firstBattleActions));
   for(auto& action:firstBattleActions)for(auto& binding:action.binding)
    if(binding.device==8)binding.device=10;
   maps[0].actions=firstBattleActions;
  }
  if(maps[1].count==16&&writableMemory(maps[1].actions,sizeof(secondBattleActions))){
   secondBattleOriginal=maps[1].actions;memcpy(secondBattleActions,secondBattleOriginal,sizeof(secondBattleActions));
   for(auto& action:secondBattleActions)for(auto& binding:action.binding)
    if(binding.device>=0&&binding.device<8)binding.device=10;
   maps[1].actions=secondBattleActions;
  }
 }
 originalSplitPoll();
 if(secondBattleOriginal)maps[1].actions=secondBattleOriginal;
 if(firstBattleOriginal)maps[0].actions=firstBattleOriginal;
 if(swapPlayers)std::swap(maps[0],maps[1]);
 if(commonOriginal)commonMap.actions=commonOriginal;
 if(!controllerTakeover||!foreground()){
  splitSourcesReady=false;
  splitPadKeys={};splitKeyboardKeys={};
  InterlockedExchange(&nativeSecondMenuActive,0);
  return;
 }
 const auto state=sample();
 if(!armed||!wasConnected){splitSourcesReady=false;InterlockedExchange(&nativeSplitMenuActive,0);InterlockedExchange(&nativeSecondMenuActive,0);return;}
 const auto& profile=*splitProfile;
 const auto now=GetTickCount64();
 const WORD keyboard=(WORD)(splitKeyboardState(profile.confirm,profile.cancel)|
                            splitLegacyMenuExtras(maps[reversed?0:1],profile.packedActions));
 const WORD pad=splitPadState(state,(WORD)lastPadButtons,profile.confirm,profile.cancel);
 if(!splitSourcesReady){
  // A key held while joining or regaining focus is not a new confirm edge.
  splitKeyboardKeys.held=keyboard;splitPadKeys.held=pad;
  splitKeyboardKeys.nextRepeat=splitPadKeys.nextRepeat=now+400;
 }
 splitSourcesReady=true;
 splitKeyboardKeys.update(keyboard,now);
 splitPadKeys.update(pad,now);
 const bool battle=haveOwner&&!menu;
 const bool firstReady=battle||splitCharP1Ready||(splitPrepareReadyBits&&(*splitPrepareReadyBits&1)!=0);
 if(nativeCommonMenu()||insideSplitPrepareScreen||splitCharMode>=0){
  InterlockedExchange(&nativeSplitMenuActive,1);
  applySplitMenuActions(secondCursor,firstReady);
 }
 if(logging&&(splitLastJoined!=(nativeSplitJoined()?1:0)||splitLastMenu!=(menu?1:0))){
  splitLastJoined=nativeSplitJoined()?1:0;splitLastMenu=menu?1:0;
  const DWORD* p1=(const DWORD*)(splitBase+splitProfile->player1Actions);
  const DWORD* p2=(const DWORD*)(splitBase+splitProfile->player2Actions);
  logf("split state joined=%d menu=%d owner=%d second=%d ready=%d actions=%08lx/%08lx/%08lx p2=%08lx/%08lx/%08lx",
       splitLastJoined,splitLastMenu,haveOwner?1:0,secondCursor?1:0,firstReady?1:0,
       p1[0],p1[profile.packedActions?1:2],p1[profile.packedActions?2:3],
       p2[0],p2[profile.packedActions?1:2],p2[profile.packedActions?2:3]);
 }
}
static void __cdecl prepareSplitMenuInputs(const DWORD* readyBits){
 // Only the native preparation handlers supply this pointer. Its low bit
 // belongs to 1P and the game updates it when a player locks/unlocks readiness.
 if(!readyBits||!insideSplitPrepareScreen)return;
 splitPrepareReadyBits=readyBits;
 if(controllerTakeover&&foreground())applySplitMenuActions(true,(*readyBits&1)!=0);
}
#ifdef _M_IX86
static void __declspec(naked) splitPrepareHook(){
 __asm {
  pushfd
  pushad
  push dword ptr [esp+48] // saved registers/flags (36) + third argument (12)
  call prepareSplitMenuInputs
  add esp,4
  popad
  popfd
  jmp dword ptr [originalSplitPrepare]
 }
}
static bool installSplitMenuRouting(BYTE* base,DWORD imageSize,const char* exe){
 if(!controllerTakeover||!isLegacyHighResolutionExecutable(exe))return false;
 const bool sm3=!_stricmp(exe,"ShinSangokumusou3.exe");
 const auto& profile=sm3?splitSsm3:splitSsm4;
 const BYTE sm3Poll[]={0x83,0xec,0x08,0x53,0x55,0x56,0x57};
 const BYTE sm4Poll[]={0x83,0xec,0x0c,0x53,0x55,0x56,0x33,0xf6};
 const BYTE sm3Prepare[]={0x83,0xec,0x14,0x55,0x56,0x8b,0x35,0xb4,0x96,0x80,0x00};
 const BYTE sm4Prepare[]={0x83,0xec,0x18,0xa1,0x68,0x6c,0x75,0x00};
 const BYTE* poll=sm3?sm3Poll:sm4Poll;
 const SIZE_T pollSize=sm3?sizeof(sm3Poll):sizeof(sm4Poll);
 const BYTE* prepare=sm3?sm3Prepare:sm4Prepare;
 const SIZE_T prepareSize=sm3?sizeof(sm3Prepare):sizeof(sm4Prepare);
 if(imageSize<profile.player2Actions+16||imageSize<profile.joinedFlags+4||
    memcmp(base+profile.poll,poll,pollSize)||memcmp(base+profile.prepare,prepare,prepareSize))return false;
 struct CallPatch {BYTE* site;BYTE original[5];BYTE replacement[5];};
 auto makeCall=[base](DWORD site,DWORD target,const void* hook){
  CallPatch patch={base+site,{0xe8},{0xe8}};
  const DWORD oldOffset=target-site-5;
  const auto newOffset=(DWORD)((uintptr_t)hook-(uintptr_t)(patch.site+5));
  memcpy(patch.original+1,&oldOffset,4);
  memcpy(patch.replacement+1,&newOffset,4);
  return patch;
 };
 const auto preparePatch=makeCall(profile.prepareCall,profile.prepare,(const void*)splitPrepareHook);
 const auto screenPatch=makeCall(profile.prepareScreenCall,profile.prepareScreen,(const void*)runSplitPrepareScreen);
 if(memcmp(preparePatch.site,preparePatch.original,5)||memcmp(screenPatch.site,screenPatch.original,5))return false;
 originalSplitPrepare=base+profile.prepare;
 originalSplitPrepareScreen=(decltype(originalSplitPrepareScreen))(base+profile.prepareScreen);
 if(!replaceCode(preparePatch.site,preparePatch.original,5,preparePatch.replacement,5))return false;
 if(!replaceCode(screenPatch.site,screenPatch.original,5,screenPatch.replacement,5)){
  replaceCode(preparePatch.site,preparePatch.replacement,5,preparePatch.original,5);
  return false;
 }
 splitBase=base;
 splitProfile=&profile;
 if(!buildInlineTrampoline(base+profile.poll,poll,pollSize,(void*)readSplitMenuInputs,(void**)&originalSplitPoll)){
  splitProfile=nullptr;
  replaceCode(screenPatch.site,screenPatch.replacement,5,screenPatch.original,5);
  replaceCode(preparePatch.site,preparePatch.replacement,5,preparePatch.original,5);
  return false;
 }
 const DWORD charPlayer=sm3?0x57240:0x6dc30;
 const auto playerPatch=makeCall(sm3?0x5797c:0x6e447,charPlayer,(const void*)runSplitCharPlayer);
 const BYTE sm3Char[]={0x8b,0x44,0x24,0x10,0x53,0x55,0x56};
 const BYTE sm4Char[]={0x8b,0x44,0x24,0x10,0x8b,0x4c,0x24,0x0c};
 originalCharPlayer=(decltype(originalCharPlayer))(base+charPlayer);
 bool characterRouting=false;
 if(replaceCode(playerPatch.site,playerPatch.original,5,playerPatch.replacement,5)){
  characterRouting=sm3?
   buildInlineTrampoline(base+0x578e0,sm3Char,sizeof(sm3Char),(void*)runSsm3CharScreen,(void**)&originalSsm3CharScreen):
   buildInlineTrampoline(base+0x6e300,sm4Char,sizeof(sm4Char),(void*)runSsm4CharScreen,(void**)&originalSsm4CharScreen);
  if(!characterRouting)replaceCode(playerPatch.site,playerPatch.replacement,5,playerPatch.original,5);
 }
 logf("split menu routing installed for %s; character selection=%d",exe,characterRouting);
 return true;
}
#endif
