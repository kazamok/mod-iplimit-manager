// Filename mod-iplimit-manager.cpp
#include "Player.h"
#include "World.h"
#include "ScriptMgr.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "AccountMgr.h"
#include "WorldSession.h"
#include "GameTime.h"
#include <unordered_map>
#include <set>
#include <mutex>
#include <sstream>

std::mutex ipMutex;
std::unordered_map<std::string, uint32> ipConnectionCount;
std::set<std::string> allowedIps;

// 강제 퇴장 예정인 플레이어 관리를 위한 구조체와 맵
struct KickInfo {
    uint32 accountId;
    uint32 kickTime;
    bool messageSent;
};
std::unordered_map<ObjectGuid, KickInfo> pendingKicks;
std::mutex kickMutex;

// 계정 인증 단계에서 IP 체크를 위한 새로운 클래스
class IpLimitManager_AccountScript : public AccountScript
{
public:
    IpLimitManager_AccountScript() : AccountScript("IpLimitManager_AccountScript") {}

    void OnAccountLogin(uint32 accountId) override
    {
        if (!sConfigMgr->GetOption<bool>("EnableIpLimitManager", true))
        {
            LOG_DEBUG("module.iplimit", "IpLimitManager disabled in config.");
            return;
        }

        std::string ip;
        std::string username;

        // 계정의 IP와 username 가져오기
        if (QueryResult result = LoginDatabase.Query("SELECT username, last_ip FROM account WHERE id = {}", accountId))
        {
            Field* fields = result->Fetch();
            username = fields[0].Get<std::string>();
            ip = fields[1].Get<std::string>();
        }
        else
        {
            return; // 계정 정보를 찾을 수 없는 경우
        }

        LOG_DEBUG("module.iplimit", "Checking login for account {} (ID: {}) from IP: {}", username, accountId, ip);

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
            LOG_INFO("module.iplimit", "Blocking login for account {} due to multiple connections from IP {}", username, ip);
            ipConnectionCount[ip]--;
            
            // 계정 즉시 차단
            uint32 duration = 1; // 1초
            time_t banTime = GameTime::GetGameTime().count();
            time_t unbanTime = banTime + duration;
            
            // 계정 잠금 및 밴 처리
            LoginDatabase.DirectExecute("UPDATE account SET locked = 1, online = 0 WHERE id = {}", accountId);
            LoginDatabase.DirectExecute("DELETE FROM account_banned WHERE id = {}", accountId);
            LoginDatabase.DirectExecute("INSERT INTO account_banned (id, bandate, unbandate, bannedby, banreason, active) "
                "VALUES ({}, {}, {}, 'IP Limit Manager', 'Multiple connections from same IP', 1)",
                accountId, banTime, unbanTime);
            
            return;
        }
    }

    // 계정 로그아웃 시 IP 카운트 감소
    void OnAccountLogout(uint32 accountId)
    {
        // 계정의 마지막 알려진 IP 가져오기
        if (QueryResult result = LoginDatabase.Query("SELECT last_ip FROM account WHERE id = {}", accountId))
        {
            std::string ip = result->Fetch()[0].Get<std::string>();

            if (!ip.empty())
            {
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
            }
        }
    }
};

class IpLimitManager_PlayerScript : public PlayerScript
{
public:
    IpLimitManager_PlayerScript() : PlayerScript("IpLimitManager_PlayerScript") {}

    void OnPlayerLogin(Player* player)
    {
        if (!sConfigMgr->GetOption<bool>("EnableIpLimitManager", true))
            return;

        std::string playerIp = player->GetSession()->GetRemoteAddress();
        uint32 accountId = player->GetSession()->GetAccountId();

        // IP가 허용 목록에 있는지 확인
        {
            std::lock_guard<std::mutex> lock(ipMutex);
            if (allowedIps.find(playerIp) != allowedIps.end())
            {
                // 모듈 알림 메시지 표시
                if (sConfigMgr->GetOption<bool>("IpLimitManager.Announce.Enable", false))
                {
                    ChatHandler(player->GetSession()).PSendSysMessage("|cff4CFF00[IP Limit Manager]|r This server is running the IP Limit Manager module.");
                }
                return;
            }
        }

        // 이미 접속중인 다른 계정이 있는지 확인
        QueryResult result = CharacterDatabase.Query(
            "SELECT c.online, c.name, c.account "
            "FROM characters c "
            "INNER JOIN acore_auth.account a ON c.account = a.id "
            "WHERE a.last_ip = '{}' AND c.online = 1 AND c.account != {}",
            playerIp, accountId);

        if (result)
        {
            Field* fields = result->Fetch();
            std::string existingCharName = fields[1].Get<std::string>();
            
            LOG_INFO("module.iplimit", "Scheduling kick for player {} (Account: {}) due to existing connection from IP {}",
                player->GetName(), accountId, playerIp);

            // 30초 후 강제 퇴장 예약
            {
                std::lock_guard<std::mutex> lock(kickMutex);
                KickInfo kickInfo;
                kickInfo.accountId = accountId;
                kickInfo.kickTime = GameTime::GetGameTime().count() + 30; // 30초 후
                kickInfo.messageSent = false;
                pendingKicks[player->GetGUID()] = kickInfo;
            }

            // 첫 번째 경고 메시지
            ChatHandler(player->GetSession()).PSendSysMessage("Warning: Another character is already online from your IP address. You will be disconnected in 30 seconds.");
        }
        else if (sConfigMgr->GetOption<bool>("IpLimitManager.Announce.Enable", false))
        {
            ChatHandler(player->GetSession()).PSendSysMessage("|cff4CFF00[IP Limit Manager]|r This server is running the IP Limit Manager module.");
        }
    }

    void OnPlayerUpdate(Player* player, uint32 diff)
    {
        std::lock_guard<std::mutex> lock(kickMutex);
        auto it = pendingKicks.find(player->GetGUID());
        if (it != pendingKicks.end())
        {
            uint32 currentTime = GameTime::GetGameTime().count();
            uint32 remainingTime = it->second.kickTime > currentTime ? it->second.kickTime - currentTime : 0;

            // 10초 남았을 때 한 번만 메시지 표시
            if (remainingTime <= 10 && !it->second.messageSent)
            {
                ChatHandler(player->GetSession()).PSendSysMessage("Warning: You will be disconnected in 10 seconds.");
                it->second.messageSent = true;
            }

            // 시간이 다 되면 강제 퇴장
            if (remainingTime == 0)
            {
                ChatHandler(player->GetSession()).PSendSysMessage("You are being disconnected due to IP limit restriction.");
                player->GetSession()->KickPlayer();
                
                // 데이터베이스에서 온라인 상태 해제
                CharacterDatabase.DirectExecute("UPDATE characters SET online = 0 WHERE guid = {}", player->GetGUID().GetCounter());
                LoginDatabase.DirectExecute("UPDATE account SET online = 0 WHERE id = {}", it->second.accountId);
                
                pendingKicks.erase(it);
            }
        }
    }
};

class IpLimitManager_CommandScript : public CommandScript
{
private:
    static bool IsValidIPv4(const std::string& ip)
    {
        std::istringstream iss(ip);
        std::string segment;
        int segmentCount = 0;
        
        while (std::getline(iss, segment, '.'))
        {
            segmentCount++;
            
            // 세그먼트가 4개를 초과하면 잘못된 형식
            if (segmentCount > 4)
                return false;

            // 빈 세그먼트 체크
            if (segment.empty())
                return false;

            // 숫자가 아닌 문자가 있는지 체크
            if (segment.find_first_not_of("0123456789") != std::string::npos)
                return false;

            // 길이가 3을 초과하면 잘못된 형식
            if (segment.length() > 3)
                return false;

            // 숫자 범위 체크 (0-255)
            int value = std::stoi(segment);
            if (value < 0 || value > 255)
                return false;

            // 01, 001 등과 같은 선행 0 체크
            if (segment.length() > 1 && segment[0] == '0')
                return false;
        }

        // 정확히 4개의 세그먼트가 있어야 함
        return segmentCount == 4;
    }

public:
    IpLimitManager_CommandScript() : CommandScript("IpLimitManager_CommandScript") {}

    Acore::ChatCommands::ChatCommandTable GetCommands() const override
    {
        using namespace Acore::ChatCommands;

        static ChatCommandTable allowIpCommandTable =
        {
            ChatCommandBuilder("add", HandleAddIpCommand, SEC_ADMINISTRATOR, Console::No),
            ChatCommandBuilder("del", HandleDelIpCommand, SEC_ADMINISTRATOR, Console::No),
            ChatCommandBuilder("listall", HandleListAllCommand, SEC_ADMINISTRATOR, Console::No)
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
        {
            handler->PSendSysMessage("Usage: .allowip add <ip>");
            handler->PSendSysMessage("Example: .allowip add 192.168.1.1");
            return false;
        }

        std::string ip = args;
        
        if (!IsValidIPv4(ip))
        {
            handler->PSendSysMessage("Error: Invalid IP address format. Please use IPv4 format (xxx.xxx.xxx.xxx)");
            handler->PSendSysMessage("Each number must be between 0 and 255.");
            return false;
        }

        LoginDatabase.Execute("REPLACE INTO custom_allowed_ips (ip) VALUES ('{}')", ip);
        allowedIps.insert(ip);
        handler->PSendSysMessage("IP %s added to allowed list.", ip.c_str());
        return true;
    }

    static bool HandleDelIpCommand(ChatHandler* handler, std::string const& args)
    {
        if (args.empty())
        {
            handler->PSendSysMessage("Usage: .allowip del <ip>");
            handler->PSendSysMessage("Example: .allowip del 192.168.1.1");
            return false;
        }

        std::string ip = args;

        if (!IsValidIPv4(ip))
        {
            handler->PSendSysMessage("Error: Invalid IP address format. Please use IPv4 format (xxx.xxx.xxx.xxx)");
            handler->PSendSysMessage("Each number must be between 0 and 255.");
            return false;
        }

        LoginDatabase.Execute("DELETE FROM custom_allowed_ips WHERE ip = '{}'", ip);
        allowedIps.erase(ip);
        handler->PSendSysMessage("IP %s removed from allowed list.", ip.c_str());
        return true;
    }

    static bool HandleListAllCommand(ChatHandler* handler, std::string const& /*args*/)
    {
        QueryResult result = LoginDatabase.Query(
            "SELECT ip, FROM_UNIXTIME(added_at) as added_date, COALESCE(description, 'No description') as desc "
            "FROM custom_allowed_ips ORDER BY added_at DESC"
        );

        if (!result)
        {
            handler->PSendSysMessage("No IPs in the allowed list.");
            return true;
        }

        handler->PSendSysMessage("=== Allowed IP List ===");
        handler->PSendSysMessage("IP Address | Added Date | Description");
        handler->PSendSysMessage("----------------------------------------");

        do
        {
            Field* fields = result->Fetch();
            std::string ip = fields[0].Get<std::string>();
            std::string addedDate = fields[1].Get<std::string>();
            std::string desc = fields[2].Get<std::string>();

            handler->PSendSysMessage("%s | %s | %s", ip.c_str(), addedDate.c_str(), desc.c_str());
        } while (result->NextRow());

        handler->PSendSysMessage("----------------------------------------");
        return true;
    }
};

void LoadAllowedIpsFromDB()
{
    try
    {
        // 테이블이 없으면 생성
        LoginDatabase.Execute(
            "CREATE TABLE IF NOT EXISTS `custom_allowed_ips` ("
            "`ip` varchar(15) NOT NULL DEFAULT '127.0.0.1',"
            "`added_at` bigint unsigned NOT NULL DEFAULT UNIX_TIMESTAMP(),"  // 추가 시간 (unix epoch)
            "`description` varchar(255) DEFAULT NULL,"  // 설명 필드
            "PRIMARY KEY (`ip`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='List of allowed IPs'"
        );

        // 데이터 로드
        QueryResult result = LoginDatabase.Query("SELECT ip FROM custom_allowed_ips");
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
                    LOG_ERROR("module.iplimit", "Invalid IP found in auth DB: {}", ip);
                }
            } while (result->NextRow());
        }

        LOG_INFO("module.iplimit", "Loaded {} allowed IPs from auth database.", count);
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
    new IpLimitManager_AccountScript();
    new IpLimitManager_PlayerScript();
    new IpLimitManager_CommandScript();
    new IpLimitManagerWorldScript();
}
