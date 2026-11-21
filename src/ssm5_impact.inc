#ifndef PADFIX_PLATFORM_ONLY
#ifdef _M_IX86
// SM5's hit/impact events are routed through the effect object constructor at
// 0x4fc570. For the four impact event kinds that reach its 0xc5/0x11 sound
// dispatch, the native path always passes a unit gain. Replace only that unit
// gain with the configured multiplier. This leaves BGM, voices, ambience and
// the game's master-volume controls untouched.
static float ssm5ImpactAudioGain=1.0f;
static BYTE* ssm5ImpactAudioReturn=nullptr;
static BYTE* ssm5ReceivedAttackAudioReturn=nullptr;
static DWORD* ssm5ImpactAudioManagerSlot=nullptr;

__declspec(naked) static void ssm5ImpactAudioHook(){
 __asm {
  fld1
  fmul dword ptr [ssm5ImpactAudioGain]
  push ecx
  mov ecx,dword ptr [ssm5ImpactAudioManagerSlot]
  mov ecx,dword ptr [ecx]
  jmp dword ptr [ssm5ImpactAudioReturn]
 }
}

// CReceivedAttack's direct impact path (0x50df90 -> 0x576db0) emits sound
// 0xc4 through a second, compact call site. Keep its original stack/register
// setup and only replace the unit gain.
__declspec(naked) static void ssm5ReceivedAttackAudioHook(){
 __asm {
  fld1
  fmul dword ptr [ssm5ImpactAudioGain]
  mov edx,dword ptr [esp+0x20]
  push ecx
  fstp dword ptr [esp]
  mov ecx,dword ptr [ssm5ImpactAudioManagerSlot]
  mov ecx,dword ptr [ecx]
  jmp dword ptr [ssm5ReceivedAttackAudioReturn]
 }
}

static bool installSsm5ImpactAudio(BYTE* base,DWORD imageSize){
 if(_stricmp(gameExeName,"SM5_WIN.exe")||impactAudioScale==100)return true;
 // IDA's code listing used the preferred image base 0x400000. The hook
 // receives the module base, so code RVAs must be expressed relative to it.
 constexpr DWORD site=0x0fc60b;
 constexpr DWORD span=9;
 constexpr DWORD attackSite=0x176e4f;
 constexpr DWORD attackSpan=10;
 if(!base||imageSize<0x10000)return false;
 static constexpr BYTE expected[span]={
  0xd9,0xe8,0x51,0x8b,0x0d,0x00,0xd6,0x75,0x00
 };
 static constexpr BYTE attackExpected[attackSpan]={
  0xd9,0xe8,0x8b,0x54,0x24,0x20,0x51,0xd9,0x1c,0x24
 };
 if(imageSize<site+span||memcmp(base+site,expected,span)||imageSize<attackSite+attackSpan||memcmp(base+attackSite,attackExpected,attackSpan)){
  // The shipped SM5 executable unpacks its text before this point. Keep the
  // fixed verified paths disabled on a layout mismatch instead of patching
  // guessed sites, which can corrupt battle-only code.
  unsigned candidates=0;
  for(DWORD rva=0;rva+8<imageSize&&candidates<24;rva++){
   if(base[rva]!=0xd9||base[rva+1]!=0xe8)continue;
   bool manager=false;
   for(unsigned j=2;j<20&&rva+j+2<imageSize;j++)
    if(base[rva+j]==0x51&&base[rva+j+1]==0x8b&&base[rva+j+2]==0x0d){manager=true;break;}
   if(manager){logf("SM5 impact candidate rva=%08lx bytes=%02x%02x%02x%02x%02x%02x",rva,base[rva],base[rva+1],base[rva+2],base[rva+3],base[rva+4],base[rva+5]);candidates++;}
  }
  logf("SM5 impact audio skipped: received-attack signature mismatch candidates=%u",candidates);
  return false;
 }
 ssm5ImpactAudioGain=std::clamp(impactAudioScale,100,800)/100.0f;
 ssm5ImpactAudioManagerSlot=(DWORD*)(base+0x35d600);
 ssm5ImpactAudioReturn=base+site+span;
 ssm5ReceivedAttackAudioReturn=base+attackSite+attackSpan;
 BYTE replacement[span]={0xE9};
 const LONG offset=(LONG)((BYTE*)ssm5ImpactAudioHook-(base+site+5));
 memcpy(replacement+1,&offset,sizeof(offset));
 for(unsigned i=5;i<span;i++)replacement[i]=0x90;
 if(!replaceCode(base+site,expected,span,replacement,span)){
  ssm5ImpactAudioReturn=nullptr;
  ssm5ReceivedAttackAudioReturn=nullptr;
  logf("SM5 impact audio hook failed to patch");
  return false;
 }
 BYTE attackReplacement[attackSpan]={0xE9};
 const LONG attackOffset=(LONG)((BYTE*)ssm5ReceivedAttackAudioHook-(base+attackSite+5));
 memcpy(attackReplacement+1,&attackOffset,sizeof(attackOffset));
 for(unsigned i=5;i<attackSpan;i++)attackReplacement[i]=0x90;
 if(!replaceCode(base+attackSite,attackExpected,attackSpan,attackReplacement,attackSpan)){
  logf("SM5 received-attack audio hook failed to patch");
  return false;
 }
 logf("SM5 impact audio gain installed: %d%%",impactAudioScale);
 return true;
}
#else
static bool installSsm5ImpactAudio(BYTE*,DWORD){return false;}
#endif
#endif
