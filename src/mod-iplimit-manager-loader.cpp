// Filename mod-iplimit-manager-loader.cpp
// 모듈 로더 클래스
// 이 클래스는 모듈이 로드될 때 실행됩니다.
// 설정에 따라 모듈을 활성화하거나 비활성화합니다.

#include "ScriptMgr.h"
#include "Config.h"
#include "Log.h"

void Addmod_iplimit_managerScripts();

class mod_iplimit_managerLoader : public ModuleScript
{
public:
    mod_iplimit_managerLoader() : ModuleScript("mod-iplimit-manager") {}

    void OnLoad()
    {
        if (sConfigMgr->GetOption<bool>("EnableIpLimitManager", true))
        {
            LOG_INFO("module", "mod-iplimit-manager loaded successfully.");
            Addmod_iplimit_managerScripts();
        }
        else
        {
            LOG_INFO("module", "mod-iplimit-manager is disabled in configuration.");
        }
    }
};

void Addmod_iplimit_managerLoader() // 모듈 로드 시 실행되는 함수
{
    new mod_iplimit_managerLoader();
}
