// Filename mod-iplimit-manager-loader.cpp
// Module loader class
// This class runs when a module is loaded.
// Enables or disables the module depending on its settings.

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

void Addmod_iplimit_managerLoader()// Function executed when module is loaded
{
    new mod_iplimit_managerLoader();
}

static void* _init = (Addmod_iplimit_managerLoader(), nullptr);
