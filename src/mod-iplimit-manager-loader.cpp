#include "ScriptMgr.h"
#include "Config.h"
#include "Log.h"

void Addmod_iplimit_managerScripts();

class mod_iplimit_managerLoader : public ModuleScript
{
public:
    mod_iplimit_managerLoader() : ModuleScript("mod-iplimit-manager") {}

    void OnLoad() override
    {
        if (sConfigMgr->GetBoolDefault("EnableIpLimitManager", true))
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

void Addmod_iplimit_managerLoader()
{
    new mod_iplimit_managerLoader();
}
