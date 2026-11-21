#ifdef _M_IX86
static BYTE* ssm6MenuBase;
static void (__thiscall *originalSsm6CharacterUpdate)(BYTE*);
static void (__stdcall *originalSsm6ScreenUpdate)(BYTE*);
static SplitMenuKeys ssm6PadKeys,ssm6KeyboardKeys;
static SplitMenuKeys ssm6SecondPadKeys;
static void* ssm6PreviousMenu;
static bool ssm6SourcesReady;
static DWORD ssm6ExtraHeld,ssm6ExtraPressed,ssm6ExtraRepeated;

struct Ssm6MenuActions {
 DWORD held,pressed,repeated;
};
static constexpr DWORD ssm6InputRva=0x7370e0;
static Ssm6MenuActions ssm6MenuActions(const SplitMenuKeys& keys,DWORD confirm,DWORD cancel,bool allowButtons,const Ssm6MenuActions& preserved={}){
 // The selector's direction group is 0x10/0x20/0x40/0x80. Keep the
 // confirm/cancel bits in the same action word when replacing that group.
 const DWORD controlMask=0xf0|confirm|cancel;
 const auto convert=[=](WORD bits){
  return DWORD((bits&1)?0x10:0)|((bits&2)?0x40:0)|((bits&4)?0x80:0)|((bits&8)?0x20:0)|
   (allowButtons&&(bits&0x10)?confirm:0)|(allowButtons&&(bits&0x20)?cancel:0);
 };
 return {(preserved.held&~controlMask)|convert(keys.held),
         (preserved.pressed&~controlMask)|convert(keys.pressed),
         (preserved.repeated&~controlMask)|convert(keys.repeated)};
}
static DWORD readSsm6ExtraKeys(){
 DWORD bits=0;
 // These are the action bits used by the DX keyboard maps. The same physical
 // keys are accepted by the character and preparation menu maps, so P2 can
 // use the configured menu shortcuts after 1P has taken the controller path.
 if(GetAsyncKeyState(VK_TAB)&0x8000)bits|=0x100000;
 if(GetAsyncKeyState(VK_SPACE)&0x8000)bits|=0x8;
 if(GetAsyncKeyState(VK_LSHIFT)&0x8000||GetAsyncKeyState(VK_RSHIFT)&0x8000)bits|=0x800000;
 if(GetAsyncKeyState('J')&0x8000)bits|=0x800000;
 if(GetAsyncKeyState('I')&0x8000)bits|=0x40000;
 return bits;
}
static bool readSsm6MenuSources(void* menu){
 std::lock_guard<std::recursive_mutex> lock(mutex);
 const auto state=sample();
 if(!armed||!wasConnected||!foreground()){
  ssm6SourcesReady=false;ssm6ExtraHeld=ssm6ExtraPressed=ssm6ExtraRepeated=0;
  InterlockedExchange(&nativeSplitMenuActive,0);return false;
 }
 // Keep the shared B/A keyboard bridge out of this callback while the
 // native 1P/2P action records are being replaced.
 InterlockedExchange(&nativeSplitMenuActive,1);
 const auto now=GetTickCount64();
 const WORD padState=splitPadState(state,(WORD)lastPadButtons,0x10,0x20);
 WORD keyboardState=splitKeyboardState(0x10,0x20);
 // The shared keyboard poll may already have consumed Enter/Esc for 1P. Once
 // controller takeover is active they belong to the joined P2 cursor here.
 if(controllerTakeover){
  if(GetAsyncKeyState(VK_RETURN)&0x8000)keyboardState|=0x10;
  if(GetAsyncKeyState(VK_ESCAPE)&0x8000)keyboardState|=0x20;
 }
 if(!ssm6SourcesReady||ssm6PreviousMenu!=menu){
  ssm6PadKeys={};ssm6KeyboardKeys={};
  ssm6PadKeys.held=padState;ssm6KeyboardKeys.held=keyboardState;
  ssm6PadKeys.nextRepeat=ssm6KeyboardKeys.nextRepeat=now+400;
 }
 ssm6PreviousMenu=menu;
 ssm6SourcesReady=true;
 ssm6PadKeys.update(padState,now);
 ssm6KeyboardKeys.update(keyboardState,now);
 const DWORD extra=readSsm6ExtraKeys();
 ssm6ExtraPressed=extra&~ssm6ExtraHeld;
 ssm6ExtraRepeated=ssm6ExtraPressed;
 ssm6ExtraHeld=extra;
 return true;
}

static void __fastcall runSsm6CharacterUpdate(BYTE* menu,void*){
 if(!readSsm6MenuSources(menu)){originalSsm6CharacterUpdate(menu);return;}
 BYTE* input=*(BYTE**)(ssm6MenuBase+ssm6InputRva);
 if(!input){originalSsm6CharacterUpdate(menu);InterlockedExchange(&nativeSecondMenuActive,0);InterlockedExchange(&nativeSplitMenuActive,0);return;}
 const auto inputWord=[input](DWORD offset){return *(DWORD*)(input+offset);};
 const auto menuWord=[menu](DWORD offset){return *(int*)(menu+offset);};
 const DWORD confirm=inputWord(0x180),cancel=inputWord(0x184);
 // The native selector reads held/pressed/repeated at +0x18/+0x1c/+0x20.
 // +0x1c and +0xdc are the pressed words, so the action record starts four
 // bytes earlier than the selector's first read.
 auto& first=*(Ssm6MenuActions*)(input+0x18);
 auto& second=*(Ssm6MenuActions*)(input+0xd8);
 const Ssm6MenuActions savedFirst=first,savedSecond=second;
 const int mode=menuWord(0x60);
 // The native selector only switches to the second action record in modes
 // 0/2, and only while its P2 selection flag is set. Mode 1 is the first
 // player's selector and must continue to consume the first record.
 const bool secondRecord=(mode==0||mode==2)&&menuWord(0x2bc)!=0;
 const bool secondReady=secondRecord&&menuWord(0x2b4)>=0;
 InterlockedExchange(&nativeSecondMenuActive,secondRecord);
 constexpr DWORD join=8;
 if(!secondRecord)first=ssm6MenuActions(ssm6PadKeys,confirm,cancel,true);
 if(secondRecord){
  const auto secondPad=sampleSecond();
  ssm6SecondPadKeys.update(splitSecondPadState(secondPad,0x10,0x20),GetTickCount64());
  SplitMenuKeys secondInput=ssm6KeyboardKeys;
  secondInput.held|=ssm6SecondPadKeys.held;
  secondInput.pressed|=ssm6SecondPadKeys.pressed;
  secondInput.repeated|=ssm6SecondPadKeys.repeated;
  second=ssm6MenuActions(secondInput,confirm,cancel,secondReady,second);
  second.held|=ssm6ExtraHeld;second.pressed|=ssm6ExtraPressed;second.repeated|=ssm6ExtraRepeated;
 }else{
  ssm6SecondPadKeys={};
  // The stock selector reads 2P's Start bit while waiting for a join.
  second={savedSecond.held&join,savedSecond.pressed&join,savedSecond.repeated&join};
 }
 originalSsm6CharacterUpdate(menu);
 first=savedFirst;
 second=savedSecond;
 InterlockedExchange(&nativeSecondMenuActive,0);
 InterlockedExchange(&nativeSplitMenuActive,0);
}

static void __stdcall runSsm6ScreenUpdate(BYTE* screen){
 const auto original=[screen](){originalSsm6ScreenUpdate(screen);};
 // Only the selected stage's preparation object and its selected submenu
 // consume this action override. The character and battle screens differ.
 constexpr DWORD stageVtable=0x617c58,prepVtable=0x6a8680;
 constexpr DWORD prepPointer=0x736f28,prepJoined=0x720c6c;
 constexpr DWORD stageSelection=0x736f74;
 if(!screen||*(BYTE**)screen!=ssm6MenuBase+stageVtable||*(DWORD*)(screen+0xb5dc)!=2){
  original();return;
 }
 BYTE* prep=*(BYTE**)(ssm6MenuBase+prepPointer);
 BYTE* child=*(BYTE**)(screen+0xb594);
 if(!prep||*(BYTE**)prep!=ssm6MenuBase+prepVtable||!prep[0x10]||!child){
  original();return;
 }
 const bool topMenu=child==prep;
 const bool joined=ssm6MenuBase[prepJoined]!=0;
 // Child preparation pages use several owner values depending on the
 // selected stage. Once P2 has joined, every child page belongs to P2's
 // action record, not only owner==1.
 const bool secondSubmenu=!topMenu&&joined;
 if((!topMenu&&!secondSubmenu)||!readSsm6MenuSources(prep)){
  original();return;
 }
 InterlockedExchange(&nativeSecondMenuActive,secondSubmenu);
 BYTE* input=*(BYTE**)(ssm6MenuBase+ssm6InputRva);
 if(!input){original();InterlockedExchange(&nativeSecondMenuActive,0);InterlockedExchange(&nativeSplitMenuActive,0);return;}
 const DWORD confirm=*(DWORD*)(input+0x180),cancel=*(DWORD*)(input+0x184);
 auto& first=*(Ssm6MenuActions*)(input+0x18);
 auto& second=*(Ssm6MenuActions*)(input+0xd8);
 const Ssm6MenuActions savedFirst=first,savedSecond=second;
 BYTE* selected=*(BYTE**)(ssm6MenuBase+stageSelection);
 const bool firstSelected=selected&&*(DWORD*)(selected+0x1c)<0x5c;
 first=ssm6MenuActions(ssm6PadKeys,confirm,cancel,true);
 if(joined){
  const auto secondPad=sampleSecond();
  ssm6SecondPadKeys.update(splitSecondPadState(secondPad,0x10,0x20),GetTickCount64());
  SplitMenuKeys secondInput=ssm6KeyboardKeys;
  secondInput.held|=ssm6SecondPadKeys.held;
  secondInput.pressed|=ssm6SecondPadKeys.pressed;
  secondInput.repeated|=ssm6SecondPadKeys.repeated;
  // Native 2P cancel on the common preparation view exits its entire screen.
  // The selected 2P submenu can receive back without dismissing that view.
  second=ssm6MenuActions(secondInput,confirm,joined?cancel:0,firstSelected||joined,savedSecond);
  second.held|=ssm6ExtraHeld;second.pressed|=ssm6ExtraPressed;second.repeated|=ssm6ExtraRepeated;
 }else {ssm6SecondPadKeys={};second={};}
 original();
 first=savedFirst;
 second=savedSecond;
 InterlockedExchange(&nativeSecondMenuActive,0);
 InterlockedExchange(&nativeSplitMenuActive,0);
}

static bool installSsm6MenuRouting(BYTE* base,DWORD imageSize){
 const BYTE characterPrefix[]={0x55,0x8b,0xec,0x83,0xe4,0xf8};
 const BYTE characterBody[]={0x83,0xec,0x3c,0x53,0x8b,0xd9,0x8b,0x03,0x8b,0x50,0x10,0x56,0x57,0xff,0xd2};
 constexpr DWORD characterUpdate=0x20a9b0;
 if(imageSize<0x7370e4||memcmp(base+characterUpdate+sizeof(characterPrefix),characterBody,sizeof(characterBody)))return false;
 ssm6MenuBase=base;
 if(!buildInlineTrampoline(base+characterUpdate,characterPrefix,sizeof(characterPrefix),
    (void*)runSsm6CharacterUpdate,(void**)&originalSsm6CharacterUpdate))return false;
 constexpr DWORD prepUpdate=0x243a40,screenSite=0x1f3498,screenUpdate=0x1fbc20;
 const BYTE prepPrefix[]={0x55,0x8b,0xec,0x83,0xe4,0xf8,0x83,0xec,0x34,0x53,0x56,0x57,0x8b,0xf9};
 BYTE expected[]={0xe8,0,0,0,0},replacement[]={0xe8,0,0,0,0};
 const DWORD oldOffset=screenUpdate-screenSite-5;
 const DWORD newOffset=(DWORD)((uintptr_t)runSsm6ScreenUpdate-(uintptr_t)(base+screenSite+5));
 memcpy(expected+1,&oldOffset,4);
 memcpy(replacement+1,&newOffset,4);
 originalSsm6ScreenUpdate=(decltype(originalSsm6ScreenUpdate))(base+screenUpdate);
 const bool preparation=!memcmp(base+prepUpdate,prepPrefix,sizeof(prepPrefix))&&
   replaceCode(base+screenSite,expected,sizeof(expected),replacement,sizeof(replacement));
 logf("SSM6 DX split menu routing installed for character selection; preparation=%d",preparation);
 return true;
}
#else
static bool installSsm6MenuRouting(BYTE*,DWORD){return false;}
#endif
