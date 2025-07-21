// mod - iplimit - manager - loader.cpp

#include "ScriptMgr.h"

// 전방 선언
void Addmod_iplimit_managerScripts();

// AzerothCore는 이 함수를 자동으로 호출하여 모듈을 등록함
extern "C" void AddSC_mod_iplimit_manager()
{
    Addmod_iplimit_managerScripts();
}
