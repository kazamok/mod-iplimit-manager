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
#include <fstream>
#include <iomanip>
#include <filesystem>
#include <ctime>
#include <vector>

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

// CSV 로깅을 위한 전역 변수
std::mutex csvMutex;
std::ofstream csvFile;
std::string currentLogDate;
std::string serverStartTime;

// CSV 로깅 유틸리티 함수
void EnsureLogDirectory()
{
    std::filesystem::path logDir = "logs/iplimit";
    if (!std::filesystem::exists(logDir))
    {
        std::filesystem::create_directories(logDir);
    }
}

std::string GetCurrentDateTime()
{
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string GetCurrentDate()
{
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d");
    return ss.str();
}

void InitializeServerStartTime()
{
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%H%M%S");
    serverStartTime = ss.str();
}

void EnsureLogFileOpen()
{
    std::string date = GetCurrentDate();
    if (date != currentLogDate || !csvFile.is_open())
    {
        if (csvFile.is_open())
        {
            csvFile.close();
        }
        
        EnsureLogDirectory();
        // 파일명에 서버 시작 시간 추가
        std::string filename = "logs/iplimit/access_log_" + date + "_" + serverStartTime + ".csv";
        bool fileExists = std::filesystem::exists(filename);
        
        csvFile.open(filename, std::ios::app);
        currentLogDate = date;
        
        if (!fileExists)
        {
            csvFile << "datetime,ip_address,account_id,account_username,action\n";
        }
    }
}

void LogAccountAction(uint32 accountId, const std::string& ip, const std::string& action)
{
    std::lock_guard<std::mutex> lock(csvMutex);
    
    try
    {
        // 계정 사용자명 조회
        std::string username;
        if (QueryResult result = LoginDatabase.Query("SELECT username FROM account WHERE id = {}", accountId))
        {
            username = result->Fetch()[0].Get<std::string>();
        }
        else
        {
            username = "unknown";
        }
        
        EnsureLogFileOpen();
        
        std::string currentDateTime = GetCurrentDateTime();
        
        csvFile << currentDateTime << ","
                << ip << ","
                << accountId << ","
                << username << ","
                << action << std::endl;
                
        // 즉시 디스크에 쓰기
        csvFile.flush();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("module.iplimit", "Failed to log account action: {}", e.what());
    }
}

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

// ChatCommand 구조체 정의
struct ChatCommand
{
    char const* Name;
    uint32 SecurityLevel;
    bool AllowConsole;
    bool (*Handler)(ChatHandler* handler, const char* args);
    std::string Help;
    ChatCommand* ChildCommands;
};

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
            
            // CSV 로그 기록
            LogAccountAction(accountId, ip, "login");
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

    void OnAccountLogout(uint32 accountId)
    {
        // 계정의 마지막 알려진 IP 가져오기
        if (QueryResult result = LoginDatabase.Query("SELECT last_ip FROM account WHERE id = {}", accountId))
        {
            std::string ip = result->Fetch()[0].Get<std::string>();

            if (!ip.empty())
            {
                // CSV 로그 기록
                LogAccountAction(accountId, ip, "logout");
                
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
                    ChatHandler(player->GetSession()).PSendSysMessage("|cff4CFF00[IP 제한 관리자]|r IP 제한 관리 모듈이 동작 중입니다.");
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
            ChatHandler(player->GetSession()).PSendSysMessage("|cff4CFF00[IP 제한 관리자]|r 경고: 현재 IP에서 이미 다른 계정이 접속 중입니다. 30초 후 접속이 종료됩니다.");
        }
        else if (sConfigMgr->GetOption<bool>("IpLimitManager.Announce.Enable", false))
        {
            ChatHandler(player->GetSession()).PSendSysMessage("|cff4CFF00[IP 제한 관리자]|r IP 제한 관리 모듈이 동작 중입니다.");
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
                ChatHandler(player->GetSession()).PSendSysMessage("|cff4CFF00[IP 제한 관리자]|r 경고: 10초 후 접속이 종료됩니다.");
                it->second.messageSent = true;
            }

            // 시간이 다 되면 강제 퇴장
            if (remainingTime == 0)
            {
                ChatHandler(player->GetSession()).PSendSysMessage("|cff4CFF00[IP 제한 관리자]|r IP 제한 정책에 따라 접속이 종료됩니다.");
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

    std::vector<Acore::ChatCommands::ChatCommandBuilder> GetCommands() const override
    {
        static std::vector<ChatCommand> allowipSubcommands =
        {
            { "add", SEC_ADMINISTRATOR, false, &HandleAddIpCommand, "IP 주소를 허용 목록에 추가합니다.", nullptr },
            { "del", SEC_ADMINISTRATOR, false, &HandleDelIpCommand, "IP 주소를 허용 목록에서 제거합니다.", nullptr },
            { nullptr, 0, false, nullptr, nullptr, nullptr }
        };

        static std::vector<ChatCommand> commandTable =
        {
            { "allowip", SEC_ADMINISTRATOR, false, nullptr, "IP 허용 목록 관리 명령어입니다.", &allowipSubcommands[0] },
            { nullptr, 0, false, nullptr, nullptr, nullptr }
        };

        std::vector<Acore::ChatCommands::ChatCommandBuilder> builders;
        builders.emplace_back(commandTable);
        return builders;
    }

    static bool HandleAddIpCommand(ChatHandler* handler, const char* args)
    {
        if (!args || !*args)
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

        // IP가 이미 존재하는지 확인
        QueryResult checkResult = LoginDatabase.Query("SELECT 1 FROM custom_allowed_ips WHERE ip = '{}'", ip);
        if (checkResult)
        {
            handler->PSendSysMessage("오류: IP {} 는 이미 허용 목록에 존재합니다.", ip);
            return false;
        }

        LoginDatabase.Execute("INSERT INTO custom_allowed_ips (ip) VALUES ('{}')", ip);
        allowedIps.insert(ip);
        handler->PSendSysMessage("IP {} 가 허용 목록에 추가되었습니다.", ip);
        return true;
    }

    static bool HandleDelIpCommand(ChatHandler* handler, const char* args)
    {
        if (!args || !*args)
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

        // IP가 존재하는지 확인
        QueryResult checkResult = LoginDatabase.Query("SELECT 1 FROM custom_allowed_ips WHERE ip = '{}'", ip);
        if (!checkResult)
        {
            handler->PSendSysMessage("오류: IP {} 는 허용 목록에 존재하지 않습니다.", ip);
            return false;
        }

        LoginDatabase.Execute("DELETE FROM custom_allowed_ips WHERE ip = '{}'", ip);
        allowedIps.erase(ip);
        handler->PSendSysMessage("IP {} 가 허용 목록에서 제거되었습니다.", ip);
        return true;
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
        InitializeServerStartTime();
        LoadAllowedIpsFromDB();
    }

    void OnShutdown() override
    {
        // 서버 종료 시 파일 스트림 정리
        std::lock_guard<std::mutex> lock(csvMutex);
        if (csvFile.is_open())
        {
            csvFile.flush();
            csvFile.close();
        }
    }
};

void Addmod_iplimit_managerScripts()
{
    new IpLimitManager_AccountScript();
    new IpLimitManager_PlayerScript();
    new IpLimitManager_CommandScript();
    new IpLimitManagerWorldScript();
}
