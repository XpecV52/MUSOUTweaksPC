#ifndef PADFIX_PLATFORM_ONLY
static BYTE* sw2InputBase;
static void (__cdecl *originalSw2Poll)();
static SplitMenuKeys sw2PadKeys,sw2KeyboardKeys;
static DWORD sw2PreviousScreen;
static bool sw2PreviousSecondCursor;
static bool sw2PreviousFirstReady;

static void __cdecl readSw2Input(){
 constexpr DWORD gameUpdate=0x36ced0,joinedFlags=0x3a9628;
 constexpr DWORD storySelect=0x84c50,freeSelect=0x862b0,preparation=0x84360,
                preparationSelect=0x7daf0,legacyShop=0x7fef0,shop=0x8adc0;
 constexpr DWORD confirm=0x80,cancel=0x40,directions=15;
 constexpr DWORD mask=confirm|cancel|directions;

 const DWORD update=*(const DWORD*)(sw2InputBase+gameUpdate);
 const DWORD screen=update-(DWORD)(uintptr_t)sw2InputBase;
 const bool menu=screen==storySelect||screen==freeSelect||screen==preparation||
                 screen==preparationSelect||screen==legacyShop||screen==shop;
 const bool joined=(*(const BYTE*)(sw2InputBase+joinedFlags)&2)!=0;
 bool secondCursor=false;
 switch(screen){
  case storySelect:
   // The selector uses different cursor values after language and route
   // transitions. Once P2 has joined, its cursor is the active one.
   secondCursor=joined;break;
  case freeSelect:secondCursor=joined;break;
  case preparation:
   // The preparation screen has several child objects and the cursor owner
   // fields are not stable across language builds. Once P2 has joined, the
   // native preparation loop is the authoritative owner for its keyboard
   // action word, including the post-ready child pages.
   secondCursor=joined;
   break;
  case preparationSelect:
   // This preparation child has its own owner field.
   secondCursor=*(const DWORD*)(sw2InputBase+0x4845e0)==1;
   break;
  case legacyShop:
   // The equipment/shop result page switches ownership after P1 confirms.
   secondCursor=*(const DWORD*)(sw2InputBase+0x484630)==1;
   break;
  case shop:
   // StateShop.cpp and all of its child handlers use this player index.
   secondCursor=*(const DWORD*)(sw2InputBase+0x485e20)==1;
   break;
 }

 bool ownsInput=false;
 {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  ownsInput=menu&&controllerTakeover&&armed&&wasConnected&&foreground();
  InterlockedExchange(&nativeSplitMenuActive,ownsInput);
  InterlockedExchange(&nativeSecondMenuActive,ownsInput&&secondCursor);
 }
 originalSw2Poll();
 std::lock_guard<std::recursive_mutex> lock(mutex);
 if(!ownsInput||!armed||!wasConnected||!foreground()){
  InterlockedExchange(&nativeSplitMenuActive,0);
  InterlockedExchange(&nativeSecondMenuActive,0);
  sw2PreviousScreen=0;
  sw2PreviousSecondCursor=false;
  sw2PreviousFirstReady=false;
  return;
 }

 const auto pad=sample();
 const auto now=GetTickCount64();
 const WORD first=splitPadState(pad,(WORD)lastPadButtons,confirm,cancel);
 const bool firstReady=screen==preparation||screen==preparationSelect
                       ?(*(const DWORD*)(sw2InputBase+0x4854fc)!=0||
                         *(const DWORD*)(sw2InputBase+0x4845e0)!=0||
                         *(const DWORD*)(sw2InputBase+0x3a9634)==1)
                       :true;
 const auto secondPad=sampleSecond();
 WORD second=(WORD)(splitKeyboardState(confirm,cancel)|
                    splitSecondPadState(secondPad,confirm,cancel));
 // Character selection accepts P2 confirmation as soon as P2 has joined.
 // Only preparation pages wait for P1's ready transition before enabling
 // Enter/Esc or their L/K aliases.
 if((screen==preparation||screen==preparationSelect)&&!firstReady)second&=~(confirm|cancel);
 if(sw2PreviousScreen!=screen){
  sw2PadKeys.held=first;
  sw2PadKeys.nextRepeat=now+400;
 }
 if(sw2PreviousScreen!=screen||sw2PreviousSecondCursor!=secondCursor||sw2PreviousFirstReady!=firstReady){
  sw2KeyboardKeys.held=second;
  sw2KeyboardKeys.nextRepeat=now+400;
 }
 sw2PreviousScreen=screen;
 sw2PreviousSecondCursor=secondCursor;
 sw2PreviousFirstReady=firstReady;
 sw2PadKeys.update(first,now);
 sw2KeyboardKeys.update(second,now);

 // The engine merges its common bindings into both player records. Keep
 // unrelated P1 actions and F12 joining, then give each cursor its own input.
 DWORD* player1=(DWORD*)(sw2InputBase+0xba6728);
 DWORD* player2=(DWORD*)(sw2InputBase+0xba6818);
 // Preserve the native join shortcut and other 1P-only menu actions.
 player1[0]=(player1[0]&~mask)|sw2PadKeys.held;
 player1[2]=(player1[2]&~mask)|sw2PadKeys.pressed;
 player1[3]=(player1[3]&~mask)|sw2PadKeys.repeated;
 if(secondCursor){
  // Keep P2's mapped TAB/J/I/Space/Shift actions from the native poll while
  // replacing only the shared direction and confirm/back bits.
  player2[0]=(player2[0]&~mask)|(secondCursor?sw2KeyboardKeys.held&mask:0);
  player2[2]=(player2[2]&~mask)|(secondCursor?sw2KeyboardKeys.pressed&mask:0);
  player2[3]=(player2[3]&~mask)|(secondCursor?sw2KeyboardKeys.repeated&mask:0);
 }else{
  player2[0]&=~mask;
  player2[2]&=~mask;
  player2[3]&=~mask;
 }
}

static void installSw2InputRouting(){
 static bool installed=false;
 if(installed||!enabled||!controllerTakeover||_stricmp(gameExeName,"SW2.exe"))return;
 BYTE* base=(BYTE*)GetModuleHandleA(nullptr);
 if(!base)return;
 const auto* dos=(const IMAGE_DOS_HEADER*)base;
 const auto* nt=(const IMAGE_NT_HEADERS*)(base+dos->e_lfanew);
 if(nt->OptionalHeader.SizeOfImage<0xba6828)return;
 constexpr BYTE pollPrefix[]={0x83,0xec,0x0c,0x53,0x55,0x56,0x33,0xf6};
 constexpr BYTE dispatch[]={0xff,0x15,0xd0,0xce,0x76,0x00};
 if(memcmp(base+0x12a2,dispatch,sizeof(dispatch)))return;
 sw2InputBase=base;
 installed=buildInlineTrampoline(base+0x1abe60,pollPrefix,sizeof(pollPrefix),
                                (void*)readSw2Input,(void**)&originalSw2Poll);
 if(installed)logf("SW2 native selection, preparation and shop input routing installed");
}
#endif
