// Filename mod-iplimit-manager.cpp
// IP Connection Limit Manager Module
// This module limits multiple client connections from the same IP.
// By default, only one connection per IP is allowed.
// Exception IPs can be added or removed by command.

#include "Player.h"
#include "World.h"
#include "ScriptMgr.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "LoginDatabase.h"
#include <unordered_map>
#include <set>
#include <mutex>

std::mutex ipMutex;
std::unordered_map<std::string, uint32> ipConnectionCount;
std::set<std::string> allowedIps;

class IpLimitManager_PlayerScript : public PlayerScript
{
public:
    IpLimitManager_PlayerScript() : PlayerScript("IpLimitManager_PlayerScript") {}

    void OnLogin(Player* player) // Check IP restrictions when player logs in
    {
        if (!sConfigMgr->GetOption<bool>("EnableIpLimitManager", true))
            return;

        std::string ip = player->GetSession()->GetRemoteAddress();

        std::lock_guard<std::mutex> lock(ipMutex);

        if (allowedIps.find(ip) != allowedIps.end())
            return;

        ipConnectionCount[ip]++;

        if (ipConnectionCount[ip] > 1)
        {
            std::string msg = sConfigMgr->GetOption<std::string>("IpLimitKickMessage", "Multiple connections from the same IP are not allowed.");
            player->GetSession()->KickPlayer(msg.c_str());
        }
    }

    void OnLogout(Player* player) // Reduce IP connection count when player logs out
    {
        std::string ip = player->GetSession()->GetRemoteAddress();

        std::lock_guard<std::mutex> lock(ipMutex);

        if (ipConnectionCount.find(ip) != ipConnectionCount.end())
        {
            if (--ipConnectionCount[ip] == 0)
            {
                ipConnectionCount.erase(ip);
            }
        }
    }
};

class IpLimitManager_CommandScript : public CommandScript
{
public:
    IpLimitManager_CommandScript() : CommandScript("IpLimitManager_CommandScript") {}

    Acore::ChatCommands::ChatCommandTable GetCommands() const override
    {
        using namespace Acore::ChatCommands;

        static ChatCommandTable allowIpCommandTable =
        {
            ChatCommandBuilder("add", HandleAddIpCommand, SEC_ADMINISTRATOR, Console::No),
            ChatCommandBuilder("del", HandleDelIpCommand, SEC_ADMINISTRATOR, Console::No)
        };

        static ChatCommandTable commandTable =
        {
            ChatCommandBuilder("allowip", allowIpCommandTable)
        };

        return commandTable;
    }

    static bool HandleAddIpCommand(ChatHandler* handler, std::string const& args) // IP Additional command processing
    {
        if (args.empty())
            return false;

        std::string ip = args;
        QueryResult result = WorldDatabase.Query("REPLACE INTO custom_allowed_ips (ip) VALUES ('%s')", ip.c_str());
        allowedIps.insert(ip);
        handler->SendSysMessage("Added to allowed IP list.");
        return true;
    }

    static bool HandleDelIpCommand(ChatHandler* handler, std::string const& args) // IP Remove command processing
    {
        if (args.empty())
            return false;

        std::string ip = args;
        WorldDatabase.Execute("DELETE FROM custom_allowed_ips WHERE ip = '%s'", ip.c_str());
        allowedIps.erase(ip);
        handler->SendSysMessage("Removed from the allowed IP list.");
        return true;
    }
};

void LoadAllowedIpsFromDB() // Load allowed IP list from DB
{
    QueryResult result = WorldDatabase.Query("SELECT ip FROM custom_allowed_ips");
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();
        allowedIps.insert(fields[0].Get<std::string>());
    } while (result->NextRow());
}

void Addmod_iplimit_managerScripts() // Register module script
{
    LoadAllowedIpsFromDB();
    new IpLimitManager_PlayerScript();
    new IpLimitManager_CommandScript();
}
