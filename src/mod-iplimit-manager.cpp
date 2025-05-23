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

// IP 유효성 검사 함수
static bool IsValidIP(const std::string& ip)
{
    std::istringstream iss(ip);
    std::string segment;
    int segmentCount = 0;
    
    while (std::getline(iss, segment, '.'))
    {
        segmentCount++;
        
        if (segmentCount > 4)
            return false;

        if (segment.empty())
            return false;

        if (segment.find_first_not_of("0123456789") != std::string::npos)
            return false;

        if (segment.length() > 3)
            return false;

        try 
        {
            int value = std::stoi(segment);
            if (value < 0 || value > 255)
                return false;

            if (segment.length() > 1 && segment[0] == '0')
                return false;
        }
        catch (...)
        {
            return false;
        }
    }

    return segmentCount == 4;
}

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

        LOG_DEBUG("module.iplimit", "Player {} (Account: {}) logging in from IP: {}", 
            player->GetName(), accountId, playerIp);

        // IP가 허용 목록에 있는지 확인
        {
            std::lock_guard<std::mutex> lock(ipMutex);
            if (allowedIps.find(playerIp) != allowedIps.end())
            {
                LOG_DEBUG("module.iplimit", "IP {} is in allowed list. Skipping restriction.", playerIp);
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
public:
    IpLimitManager_CommandScript() : CommandScript("IpLimitManager_CommandScript") {}

    Acore::ChatCommands::ChatCommandTable GetCommands() const override
    {
        using namespace Acore::ChatCommands;

        static ChatCommandTable allowIpCommandTable =
        {
            { "add", HandleAddIpCommand, SEC_ADMINISTRATOR, Console::No },
            { "del", HandleDelIpCommand, SEC_ADMINISTRATOR, Console::No },
            { "listall", HandleListAllCommand, SEC_ADMINISTRATOR, Console::No }
        };

        static ChatCommandTable commandTable =
        {
            { "allowip", allowIpCommandTable }
        };
 
        return commandTable;
    }

    static bool HandleAddIpCommand(ChatHandler* handler, std::string const& args)
    {
        if (args.empty())
        {
            handler->PSendSysMessage("사용법: .allowip add <ip>");
            handler->PSendSysMessage("예시: .allowip add 192.168.1.1");
            return false;
        }

        std::string ip = args;
        
        if (!IsValidIP(ip))
        {
            handler->PSendSysMessage("오류: 잘못된 IP 주소 형식입니다. IPv4 형식을 사용해주세요.");
            return false;
        }

        LoginDatabase.Execute("REPLACE INTO custom_allowed_ips (ip) VALUES ('{}')", ip);
        allowedIps.insert(ip);
        handler->PSendSysMessage("IP {} 가 허용 목록에 추가되었습니다.", ip);
        return true;
    }

    static bool HandleDelIpCommand(ChatHandler* handler, std::string const& args)
    {
        if (args.empty())
        {
            handler->PSendSysMessage("사용법: .allowip del <ip>");
            handler->PSendSysMessage("예시: .allowip del 192.168.1.1");
            return false;
        }

        std::string ip = args;

        if (!IsValidIP(ip))
        {
            handler->PSendSysMessage("오류: 잘못된 IP 주소 형식입니다. IPv4 형식을 사용해주세요.");
            return false;
        }

        LoginDatabase.Execute("DELETE FROM custom_allowed_ips WHERE ip = '{}'", ip);
        allowedIps.erase(ip);
        handler->PSendSysMessage("IP {} 가 허용 목록에서 제거되었습니다.", ip);
        return true;
    }

    static bool HandleListAllCommand(ChatHandler* handler, std::string const& /*args*/)
    {
        try 
        {
            QueryResult testConnection = LoginDatabase.Query("SELECT 1");
            if (!testConnection)
            {
                LOG_ERROR("module.iplimit", "데이터베이스 연결이 불가능합니다");
                handler->PSendSysMessage("|cFFFF0000오류:|r 데이터베이스 연결에 실패했습니다.");
                return false;
            }

            QueryResult result = LoginDatabase.Query(
                "SELECT ip, COALESCE(description, '설명 없음') as desc "
                "FROM custom_allowed_ips"
            );

            if (!result)
            {
                LOG_INFO("module.iplimit", "허용된 IP 목록이 비어있습니다");
                handler->PSendSysMessage("|cFF00FFFF알림:|r 허용된 IP 목록이 비어있습니다.");
                return true;
            }

            uint32 count = 0;
            handler->PSendSysMessage("|cFF00FF00=== 허용된 IP 목록 ===|r");
            handler->PSendSysMessage("IP 주소 | 설명");
            handler->PSendSysMessage("----------------------------------------");

            do
            {
                Field* fields = result->Fetch();
                std::string ip = fields[0].Get<std::string>();
                std::string desc = fields[1].Get<std::string>();

                handler->PSendSysMessage("|cFFFFFF00{}|r | {}", ip, desc);
                count++;
            } while (result->NextRow());

            handler->PSendSysMessage("----------------------------------------");
            handler->PSendSysMessage("총 |cFF00FF00{}|r개의 IP가 등록되어 있습니다.", count);
            
            return true;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("module.iplimit", "IP 목록 조회 중 오류 발생: {}", e.what());
            handler->PSendSysMessage("|cFFFF0000오류:|r IP 목록을 조회하는 중 오류가 발생했습니다.");
            return false;
        }
    }
};

void LoadAllowedIpsFromDB()
{
    try
    {
        QueryResult testConnection = LoginDatabase.Query("SELECT 1");
        if (!testConnection)
        {
            LOG_ERROR("module.iplimit", "데이터베이스 연결이 불가능하여 허용된 IP를 로드할 수 없습니다");
            return;
        }

        LOG_INFO("module.iplimit", "IP Limit Manager 데이터베이스 초기화 중...");
        
        // 테이블 존재 여부 확인
        QueryResult checkTable = LoginDatabase.Query("SHOW TABLES LIKE 'custom_allowed_ips'");
        if (!checkTable)
        {
            LOG_INFO("module.iplimit", "custom_allowed_ips 테이블 생성 중...");
            LoginDatabase.Execute(
                "CREATE TABLE IF NOT EXISTS `custom_allowed_ips` ("
                "`ip` varchar(15) NOT NULL DEFAULT '127.0.0.1',"
                "`description` varchar(255) DEFAULT NULL,"
                "PRIMARY KEY (`ip`)"
                ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"
            );
            
            // 기본 localhost IP 추가
            LoginDatabase.Execute(
                "INSERT IGNORE INTO custom_allowed_ips (ip, description) "
                "VALUES ('127.0.0.1', 'Default localhost')"
            );
            LOG_INFO("module.iplimit", "테이블 생성 및 기본 데이터 추가 완료");
        }

        // 데이터 로드
        QueryResult result = LoginDatabase.Query("SELECT ip FROM custom_allowed_ips");
        uint32 count = 0;

        allowedIps.clear(); // 기존 데이터 초기화

        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                std::string ip = fields[0].Get<std::string>();

                if (!ip.empty() && IsValidIP(ip))
                {
                    allowedIps.insert(ip);
                    ++count;
                    LOG_DEBUG("module.iplimit", "허용된 IP 로드: {}", ip);
                }
                else
                {
                    LOG_ERROR("module.iplimit", "잘못된 IP 형식 발견: {}", ip);
                }
            } while (result->NextRow());
        }

        LOG_INFO("module.iplimit", "데이터베이스에서 {}개의 허용된 IP를 로드했습니다", count);
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("module.iplimit", "허용된 IP 로드 중 오류 발생: {}", e.what());
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
