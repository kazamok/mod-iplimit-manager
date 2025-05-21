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

    void OnLogin(Player* player) override
    {
        if (!sConfigMgr->GetBoolDefault("EnableIpLimitManager", true))
            return;

        std::string ip = player->GetSession()->GetRemoteAddress();

        std::lock_guard<std::mutex> lock(ipMutex);

        if (allowedIps.find(ip) != allowedIps.end())
            return;

        ipConnectionCount[ip]++;

        if (ipConnectionCount[ip] > 1)
        {
            std::string msg = sConfigMgr->GetStringDefault("IpLimitKickMessage", "Multiple connections from the same IP are not allowed.");
            player->GetSession()->KickPlayer(msg.c_str());
        }
    }

    void OnLogout(Player* player) override
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

    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> allowIpCommandTable =
        {
            { "add", SEC_ADMINISTRATOR, true, &HandleAddIpCommand, "" },
            { "del", SEC_ADMINISTRATOR, true, &HandleDelIpCommand, "" },
        };

        static std::vector<ChatCommand> commandTable =
        {
            { "allowip", SEC_ADMINISTRATOR, true, NULL, "", allowIpCommandTable }
        };

        return commandTable;
    }

    static bool HandleAddIpCommand(ChatHandler* handler, std::string const& args)
    {
        if (args.empty())
            return false;

        std::string ip = args;
        QueryResult result = WorldDatabase.PQuery("REPLACE INTO custom_allowed_ips (ip) VALUES ('%s')", ip.c_str());
        allowedIps.insert(ip);
        handler->SendSysMessage("허용 IP 목록에 추가되었습니다.");
        return true;
    }

    static bool HandleDelIpCommand(ChatHandler* handler, std::string const& args)
    {
        if (args.empty())
            return false;

        std::string ip = args;
        WorldDatabase.PExecute("DELETE FROM custom_allowed_ips WHERE ip = '%s'", ip.c_str());
        allowedIps.erase(ip);
        handler->SendSysMessage("허용 IP 목록에서 제거되었습니다.");
        return true;
    }
};

void LoadAllowedIpsFromDB()
{
    QueryResult result = WorldDatabase.Query("SELECT ip FROM custom_allowed_ips");
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();
        allowedIps.insert(fields[0].GetString());
    } while (result->NextRow());
}

void Addmod_iplimit_managerScripts()
{
    LoadAllowedIpsFromDB();
    new IpLimitManager_PlayerScript();
    new IpLimitManager_CommandScript();
}
