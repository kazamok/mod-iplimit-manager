// Filename mod-iplimit-manager.cpp
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

    void OnLogin(Player* player)
    {
        // 모듈 알림 기능
        if (sConfigMgr->GetOption<bool>("IpLimitManager.Announce.Enable", false))
        {
            ChatHandler(player->GetSession()).PSendSysMessage("|cff4CFF00[IP Limit Manager]|r This server is running the IP Limit Manager module.");
        }

        if (!sConfigMgr->GetOption<bool>("EnableIpLimitManager", true))
        {
            LOG_DEBUG("module.iplimit", "IpLimitManager disabled in config.");
            return;
        }

        std::string ip = player->GetSession()->GetRemoteAddress();
        LOG_DEBUG("module.iplimit", "OnLogin() called for player {} with IP: {}", player->GetName(), ip);

        std::lock_guard<std::mutex> lock(ipMutex);

        if (allowedIps.find(ip) != allowedIps.end())
        {
            LOG_DEBUG("module.iplimit", "IP {} is in allowed list. Skipping restriction.", ip);
            return;
        }

        ipConnectionCount[ip]++;
        LOG_DEBUG("module.iplimit", "IP {} current connection count: {}", ip, ipConnectionCount[ip]);

        if (ipConnectionCount[ip] > 1)
        {
            std::string msg = sConfigMgr->GetOption<std::string>("IpLimitKickMessage", "Multiple connections from the same IP are not allowed.");
            LOG_INFO("module.iplimit", "Kicking player {} due to multiple connections from IP {}", player->GetName(), ip);
            player->GetSession()->KickPlayer();
            ChatHandler(player->GetSession()).PSendSysMessage(msg.c_str());
        }
        else
        {
            LOG_DEBUG("module.iplimit", "Player {} login allowed. No IP conflict.", player->GetName());
        }
    }

    void OnLogout(Player* player)
    {
        std::string ip = player->GetSession()->GetRemoteAddress();
        LOG_DEBUG("module.iplimit", "OnLogout() called for player {} with IP: {}", player->GetName(), ip);

        std::lock_guard<std::mutex> lock(ipMutex);

        if (ipConnectionCount.find(ip) != ipConnectionCount.end())
        {
            ipConnectionCount[ip]--;
            LOG_DEBUG("module.iplimit", "IP {} decremented connection count: {}", ip, ipConnectionCount[ip]);

            if (ipConnectionCount[ip] <= 0)
            {
                ipConnectionCount.erase(ip);
                LOG_DEBUG("module.iplimit", "IP {} removed from connection count map.", ip);
            }
        }
        else
        {
            LOG_WARN("module.iplimit", "OnLogout: IP {} not found in map.", ip);
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

    static bool HandleAddIpCommand(ChatHandler* handler, std::string const& args)
    {
        if (args.empty())
            return false;

        std::string ip = args;
        CharacterDatabase.Execute("REPLACE INTO custom_allowed_ips (ip) VALUES ('" + ip + "')");
        allowedIps.insert(ip);
        handler->SendSysMessage("Added to allowed IP list.");
        return true;
    }

    static bool HandleDelIpCommand(ChatHandler* handler, std::string const& args)
    {
        if (args.empty())
            return false;

        std::string ip = args;
        CharacterDatabase.Execute("DELETE FROM custom_allowed_ips WHERE ip = '" + ip + "'");
        allowedIps.erase(ip);
        handler->SendSysMessage("Removed from the allowed IP list.");
        return true;
    }
};

void LoadAllowedIpsFromDB()
{
    try
    {
        // 테이블이 없으면 생성
        CharacterDatabase.Execute(
            "CREATE TABLE IF NOT EXISTS `custom_allowed_ips` ("
            "`ip` varchar(15) NOT NULL DEFAULT '127.0.0.1',"
            "PRIMARY KEY (`ip`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='List of allowed IPs'"
        );

        // 데이터 로드
        QueryResult result = CharacterDatabase.Query("SELECT ip FROM custom_allowed_ips");
        uint32 count = 0;

        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                std::string ip = fields[0].Get<std::string>();

                if (!ip.empty() && ip.length() <= 15)
                {
                    allowedIps.insert(ip);
                    ++count;
                }
                else
                {
                    LOG_ERROR("module.iplimit", "Invalid IP found in character DB: {}", ip);
                }
            } while (result->NextRow());
        }

        LOG_INFO("module.iplimit", "Loaded {} allowed IPs from character database.", count);
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("module.iplimit", "Exception occurred while loading allowed IPs: {}", e.what());
    }
    catch (...)
    {
        LOG_ERROR("module.iplimit", "Unknown exception occurred in LoadAllowedIpsFromDB!");
    }
}


// Load IP list only after full DB initialization
class IpLimitManagerWorldScript : public WorldScript
{
public:
    IpLimitManagerWorldScript() : WorldScript("IpLimitManagerWorldScript") {}

    void OnStartup() override
    {
        LoadAllowedIpsFromDB();
    }
};

void Addmod_iplimit_managerScripts()
{
    new IpLimitManager_PlayerScript();
    new IpLimitManager_CommandScript();
    new IpLimitManagerWorldScript();
}
