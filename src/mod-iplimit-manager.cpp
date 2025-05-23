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
        if (!sConfigMgr->GetOption<bool>("EnableIpLimitManager", true))
        {
            LOG_INFO("module", "IpLimitManager disabled in config.");
            return;
        }

        std::string ip = player->GetSession()->GetRemoteAddress();
        LOG_INFO("module", "OnLogin() called for player '{}', IP: {}", player->GetName(), ip);

        std::lock_guard<std::mutex> lock(ipMutex);

        if (allowedIps.find(ip) != allowedIps.end())
        {
            LOG_INFO("module", "IP {} is in allowed list. Skipping restriction.", ip);
            return;
        }

        ipConnectionCount[ip]++;
        LOG_INFO("module", "IP {} current connection count: {}", ip, ipConnectionCount[ip]);

        if (ipConnectionCount[ip] > 1)
        {
            std::string msg = sConfigMgr->GetOption<std::string>("IpLimitKickMessage", "Multiple connections from the same IP are not allowed.");
            LOG_INFO("module", "Kicking player '{}'. Reason: {}", player->GetName(), msg);
            player->GetSession()->KickPlayer(msg.c_str());
        }
        else
        {
            LOG_INFO("module", "Player '{}' login allowed. No IP conflict.", player->GetName());
        }
    }

    void OnLogout(Player* player)
    {
        std::string ip = player->GetSession()->GetRemoteAddress();
        LOG_INFO("module", "OnLogout() called for player '{}', IP: {}", player->GetName(), ip);

        std::lock_guard<std::mutex> lock(ipMutex);

        if (ipConnectionCount.find(ip) != ipConnectionCount.end())
        {
            ipConnectionCount[ip]--;
            LOG_INFO("module", "IP {} decremented connection count: {}", ip, ipConnectionCount[ip]);

            if (ipConnectionCount[ip] <= 0)
            {
                ipConnectionCount.erase(ip);
                LOG_INFO("module", "IP {} removed from connection count map.", ip);
            }
        }
        else
        {
            LOG_WARN("module", "OnLogout: IP {} not found in map.", ip);
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
        WorldDatabase.Execute("REPLACE INTO custom_allowed_ips (ip) VALUES ('" + ip + "')");
        allowedIps.insert(ip);
        handler->SendSysMessage("Added to allowed IP list.");
        return true;
    }

    static bool HandleDelIpCommand(ChatHandler* handler, std::string const& args)
    {
        if (args.empty())
            return false;

        std::string ip = args;
        WorldDatabase.Execute("DELETE FROM custom_allowed_ips WHERE ip = '" + ip + "'");
        allowedIps.erase(ip);
        handler->SendSysMessage("Removed from the allowed IP list.");
        return true;
    }
};

void LoadAllowedIpsFromDB()
{
    try
    {
        // 첫 번째 쿼리: 테이블 존재 확인
        if (QueryResult tableCheck = WorldDatabase.Query("SHOW TABLES LIKE 'custom_allowed_ips'"))
        {
            if (tableCheck->GetRowCount() == 0)
            {
                LOG_WARN("server.loading", ">> Table `custom_allowed_ips` does not exist. Creating it...");
                WorldDatabase.Execute(
                    "CREATE TABLE IF NOT EXISTS `custom_allowed_ips` ("
                    "`ip` varchar(15) NOT NULL DEFAULT '127.0.0.1',"
                    "PRIMARY KEY (`ip`)"
                    ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='List of allowed IPs'"
                );
                LOG_INFO("server.loading", ">> Created `custom_allowed_ips` table.");
                return;
            }
        }
        else
        {
            LOG_ERROR("server.loading", ">> Unable to query WorldDatabase. Is it connected?");
            return;
        }

        // 두 번째 쿼리: 데이터 로드
        if (QueryResult result = WorldDatabase.Query("SELECT ip FROM custom_allowed_ips"))
        {
            uint32 count = 0;
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
                    LOG_ERROR("server.loading", ">> Invalid IP found in DB: %s", ip.c_str());
                }
            } while (result->NextRow());

            LOG_INFO("server.loading", ">> Loaded %u allowed IPs from DB.", count);
        }
        else
        {
            LOG_WARN("server.loading", ">> No allowed IPs found in `custom_allowed_ips`.");
        }
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("server.loading", ">> Exception occurred while loading allowed IPs: %s", e.what());
    }
    catch (...)
    {
        LOG_ERROR("server.loading", ">> Unknown exception occurred in LoadAllowedIpsFromDB!");
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
