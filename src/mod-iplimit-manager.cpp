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
#include <deque>

std::mutex ipMutex;
std::unordered_map<std::string, uint32> ipConnectionCount;
std::unordered_map<std::string, uint32> ipMaxConnectionLimits;
std::unordered_set<std::string> rateLimitedIps;

// IP별 제한 설정을 위한 구조체
struct IpLimitSettings
{
    uint32 maxConnections;
    uint32 maxUniqueAccounts;
};
std::unordered_map<std::string, IpLimitSettings> allowedIps;

// IP별 고유 계정 로그인 기록을 저장하기 위한 데이터 구조
// <IP 주소, <(계정 ID, 로그인 시간) 목록>>
std::unordered_map<std::string, std::deque<std::pair<uint32, time_t>>> ipLoginHistory;


// 강제 퇴장 예정인 플레이어 관리를 위한 구조체와 맵
enum class KickReason
{
    CONCURRENT_LIMIT,
    RATE_LIMIT
};

struct KickInfo {
    uint32 accountId;
    uint32 kickTime;
    bool messageSent;
    KickReason reason;
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
    // logs 폴더가 없으면 생성하기, 그외 각종 시스템 로그도 여기에 저장됨
    std::filesystem::path baseLogDir = "logs";
    if (!std::filesystem::exists(baseLogDir))
    {
        std::filesystem::create_directory(baseLogDir);
        LOG_INFO("module.iplimit", "Created base log directory: {}", baseLogDir.string());
    }

    std::filesystem::path logDir = "logs/iplimit";
    if (!std::filesystem::exists(logDir))
    {
        std::filesystem::create_directories(logDir);
        LOG_INFO("module.iplimit", "Created IPLimit log directory: {}", logDir.string());
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

        if (QueryResult result = LoginDatabase.Query("SELECT username, last_ip FROM account WHERE id = {}", accountId))
        {
            Field* fields = result->Fetch();
            username = fields[0].Get<std::string>();
            ip = fields[1].Get<std::string>();
            LogAccountAction(accountId, ip, "login");
        }
        else
        {
            return;
        }

        LOG_DEBUG("module.iplimit", "Checking login for account {} (ID: {}) from IP: {}", username, accountId, ip);

        std::lock_guard<std::mutex> lock(ipMutex);

        // IP에 적용할 제한 설정
        uint32 maxConnections;
        uint32 maxUniqueAccounts;
        uint32 timeWindow = sConfigMgr->GetOption<uint32>("IpLimitManager.RateLimit.TimeWindowSeconds", 3600);

        auto it = allowedIps.find(ip);
        if (it != allowedIps.end())
        {
            // 화이트리스트에 있는 경우: DB 값 사용
            maxConnections = it->second.maxConnections;
            maxUniqueAccounts = it->second.maxUniqueAccounts;
            LOG_DEBUG("module.iplimit", "IP {} is in allowed list. Limits: max_conn={}, max_unique={}", ip, maxConnections, maxUniqueAccounts);
        }
        else
        {
            // 화이트리스트에 없는 경우: 설정 파일 값 사용
            maxConnections = sConfigMgr->GetOption<uint32>("IpLimitManager.Max.Account", 1);
            maxUniqueAccounts = sConfigMgr->GetOption<uint32>("IpLimitManager.RateLimit.MaxUniqueAccounts", 1);
        }

        // PlayerScript에서 사용할 수 있도록 최대 연결 수를 저장
        ipMaxConnectionLimits[ip] = maxConnections;

        // --- 1. IP별 고유 계정 로그인 빈도 제한 ---
        if (sConfigMgr->GetOption<bool>("IpLimitManager.RateLimit.Enable", true))
        {
            time_t now = GameTime::GetGameTime().count();
            
            auto& history = ipLoginHistory[ip];
            history.erase(std::remove_if(history.begin(), history.end(),
                [now, timeWindow](const auto& record) {
                    return (now - record.second) > timeWindow;
                }), history.end());

            std::set<uint32> uniqueAccounts;
            for (const auto& record : history)
            {
                uniqueAccounts.insert(record.first);
            }

            bool isNewAccount = uniqueAccounts.find(accountId) == uniqueAccounts.end();
            if (isNewAccount && uniqueAccounts.size() >= maxUniqueAccounts)
            {
                LOG_INFO("module.iplimit", "IPLimit: IP {} 에서 최근 {}초 동안 허용된 고유 계정 수({})를 초과했습니다. PlayerScript에서 처리하도록 플래그를 설정합니다.",
                    ip, timeWindow, maxUniqueAccounts);
                rateLimitedIps.insert(ip);
            }

            history.push_back({accountId, now});
        }

        // --- 2. 동시 접속 제한 ---
        if (sConfigMgr->GetOption<bool>("IpLimitManager.Max.Account.Enable", true))
        {
            ipConnectionCount[ip]++;
            LOG_DEBUG("module.iplimit", "IP {} current connection count: {}", ip, ipConnectionCount[ip]);
        }
    }

    void OnAccountLogout(uint32 accountId)
    {
        if (!sConfigMgr->GetOption<bool>("EnableIpLimitManager", true))
        {
            return;
        }

        // 계정의 마지막 알려진 IP 가져오기
        if (QueryResult result = LoginDatabase.Query("SELECT last_ip FROM account WHERE id = {}", accountId))
        {
            std::string ip = result->Fetch()[0].Get<std::string>();

            if (!ip.empty())
            {
                // CSV 로그 기록
                LogAccountAction(accountId, ip, "logout");
                
                if (sConfigMgr->GetOption<bool>("IpLimitManager.Max.Account.Enable", true))
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

        // IP가 허용 목록에 있는지 확인 (로직 수정: 모든 IP에 대해 중복 접속 확인)
        {
            std::lock_guard<std::mutex> lock(ipMutex);
            auto it = allowedIps.find(playerIp);
            if (it != allowedIps.end())
            {
                LOG_DEBUG("module.iplimit", "IP {} is in allowed list. Checking for duplicate connections.", playerIp);
            }
        }

        // 모듈 알림 메시지 표시
        if (sConfigMgr->GetOption<bool>("IpLimitManager.Announce.Enable", true))
        {
            bool maxConnEnabled = sConfigMgr->GetOption<bool>("IpLimitManager.Max.Account.Enable", true);
            bool rateLimitEnabled = sConfigMgr->GetOption<bool>("IpLimitManager.RateLimit.Enable", true);
            std::string msg = "|cff4CFF00[IP Limit Manager]|r ";
            bool active = false;

            if (maxConnEnabled && rateLimitEnabled)
            {
                msg += "이 서버는 동시 접속 및 로그인 빈도 제한이 활성화되어 있습니다.";
                active = true;
            }
            else if (maxConnEnabled)
            {
                msg += "이 서버는 동시 접속 제한이 활성화되어 있습니다.";
                active = true;
            }
            else if (rateLimitEnabled)
            {
                msg += "이 서버는 로그인 빈도 제한이 활성화되어 있습니다.";
                active = true;
            }

            if (active)
            {
                ChatHandler(player->GetSession()).PSendSysMessage(msg);
            }
        }

        // --- 제한 로직 시작 ---
        bool kickPlayer = false;
        KickReason reason = KickReason::CONCURRENT_LIMIT; // 기본값
        std::string reasonStrForLog;

        // 1. 고유 계정 로그인 빈도 제한 확인
        if (sConfigMgr->GetOption<bool>("IpLimitManager.RateLimit.Enable", true))
        {
            std::lock_guard<std::mutex> lock(ipMutex);
            if (rateLimitedIps.count(playerIp))
            {
                kickPlayer = true;
                reason = KickReason::RATE_LIMIT;
                reasonStrForLog = "고유 계정 로그인 빈도 제한 초과";
            }
        }

        // 2. 동시 접속 제한 확인 (고유 계정 제한에 걸리지 않은 경우에만)
        if (!kickPlayer && sConfigMgr->GetOption<bool>("IpLimitManager.Max.Account.Enable", true))
        {
            uint32 maxConnections = 1;
            {
                std::lock_guard<std::mutex> lock(ipMutex);
                auto it = ipMaxConnectionLimits.find(playerIp);
                if (it != ipMaxConnectionLimits.end())
                {
                    maxConnections = it->second;
                }
                else 
                {
                    maxConnections = sConfigMgr->GetOption<uint32>("IpLimitManager.Max.Account", 1);
                }
            }

            QueryResult result = CharacterDatabase.Query(
                "SELECT COUNT(c.guid) FROM characters c "
                "INNER JOIN acore_auth.account a ON c.account = a.id "
                "WHERE a.last_ip = '{}' AND c.online = 1",
                playerIp);

            if (result)
            {
                uint32 onlineCount = result->Fetch()[0].Get<uint32>();
                if (onlineCount > maxConnections)
                {
                    kickPlayer = true;
                    reason = KickReason::CONCURRENT_LIMIT;
                    reasonStrForLog = "동시 접속 제한 초과";
                }
            }
        }

        // 강제 퇴장 처리
        if (kickPlayer)
        {
            LOG_INFO("module.iplimit", "IPLimit: {} ({}) 로 인해 캐릭터 ({})가 30초 후 강제 퇴장이 예약됩니다.", playerIp, reasonStrForLog, player->GetName());

            {
                std::lock_guard<std::mutex> lock(kickMutex);
                KickInfo kickInfo;
                kickInfo.accountId = accountId;
                kickInfo.kickTime = GameTime::GetGameTime().count() + 30;
                kickInfo.messageSent = false;
                kickInfo.reason = reason;
                pendingKicks[player->GetGUID()] = kickInfo;
            }

            std::string msg = "|cff4CFF00[IP Limit Manager]|r 경고: ";
            if (reason == KickReason::CONCURRENT_LIMIT)
            {
                msg += "허용된 최대 동시 접속 수를 초과했습니다.";
            }
            else // KickReason::RATE_LIMIT
            {
                msg += "짧은 시간 내에 너무 많은 계정으로 접속했습니다.";
            }
            msg += " 30초 후 연결이 끊어집니다.";
            ChatHandler(player->GetSession()).PSendSysMessage(msg);
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

            if (remainingTime <= 10 && !it->second.messageSent)
            {
                ChatHandler(player->GetSession()).PSendSysMessage("|cff4CFF00[IP Limit Manager]|r 경고: 10초 후에 연결이 끊어집니다.");
                it->second.messageSent = true;
            }

            if (remainingTime == 0)
            {
                std::string msg = "|cff4CFF00[IP Limit Manager]|r ";
                if (it->second.reason == KickReason::CONCURRENT_LIMIT)
                {
                    msg += "최대 동시 접속 제한으로 인해 연결이 끊어졌습니다.";
                }
                else // KickReason::RATE_LIMIT
                {
                    msg += "로그인 빈도 제한으로 인해 연결이 끊어졌습니다.";
                }
                ChatHandler(player->GetSession()).PSendSysMessage(msg);
                
                player->GetSession()->KickPlayer();
                
                CharacterDatabase.DirectExecute("UPDATE characters SET online = 0 WHERE guid = {}", player->GetGUID().GetCounter());
                LoginDatabase.DirectExecute("UPDATE account SET online = 0 WHERE id = {}", it->second.accountId);
                
                pendingKicks.erase(it);
            }
        }
    }

    // 캐릭터(플레이어) 종료 시 로그아웃 액션이 CSV에 정상적으로 기록됩니다.
    void OnPlayerLogout(Player* player)
    {
        if (!sConfigMgr->GetOption<bool>("EnableIpLimitManager", true))
            return;

        uint32 accountId = player->GetSession()->GetAccountId();
        std::string playerIp = player->GetSession()->GetRemoteAddress();

        // 로그아웃 액션 기록
        LogAccountAction(accountId, playerIp, "logout");

        // 메모리 정리를 위해 맵에서 IP 제거
        {
            std::lock_guard<std::mutex> lock(ipMutex);
            ipMaxConnectionLimits.erase(playerIp);
            rateLimitedIps.erase(playerIp);
        }

        // 강제 퇴장 목록에서 제거
        {
            std::lock_guard<std::mutex> lock(kickMutex);
            pendingKicks.erase(player->GetGUID());
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

        // help string 인자를 제거하여 기본 형태로 수정
        static ChatCommandTable allowIpCommandTable =
        {
            { "append", HandleAddIpCommand, SEC_ADMINISTRATOR, Console::Yes },
            { "remove", HandleDelIpCommand, SEC_ADMINISTRATOR, Console::Yes },
            { "show",   HandleShowIpCommand, SEC_ADMINISTRATOR, Console::Yes }
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
            handler->PSendSysMessage("사용법: .allowip append <ip> [max_conn] [max_unique]");
            handler->PSendSysMessage("예시: .allowip append 192.168.1.1 3 5");
            return false;
        }

        std::stringstream ss(args);
        std::string ip;
        uint32 max_connections = 2; // 기본값
        uint32 max_unique_accounts = 1; // 기본값

        ss >> ip;
        ss >> max_connections;
        ss >> max_unique_accounts;
        
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

        LoginDatabase.Execute("INSERT INTO custom_allowed_ips (ip, max_connections, max_unique_accounts) VALUES ('{}', {}, {})", ip, max_connections, max_unique_accounts);
        allowedIps[ip] = {max_connections, max_unique_accounts};
        handler->PSendSysMessage("IP {} 가 허용 목록에 추가되었습니다. (최대 접속: {}, 최대 고유 계정: {})", ip, max_connections, max_unique_accounts);
        return true;
    }

    static bool HandleDelIpCommand(ChatHandler* handler, std::string const& args)
    {
        if (args.empty())
        {
            handler->PSendSysMessage("사용법: .allowip remove <ip>");
            handler->PSendSysMessage("예시: .allowip remove 192.168.1.1");
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

    static bool HandleShowIpCommand(ChatHandler* handler, std::string const& args)
    {
        // 디버깅을 위한 로그 추가
        LOG_INFO("module.iplimit", "HandleShowIpCommand 함수 실행 시작");

        // 테이블 존재 여부 먼저 확인
        QueryResult tableCheck = LoginDatabase.Query("SHOW TABLES LIKE 'custom_allowed_ips'");
        if (!tableCheck)
        {
            handler->PSendSysMessage("|cFFFF0000오류:|r custom_allowed_ips 테이블이 존재하지 않습니다.");
            LOG_ERROR("module.iplimit", "custom_allowed_ips 테이블이 존재하지 않음");
            return false;
        }

        LOG_INFO("module.iplimit", "테이블 존재 확인됨, 데이터 조회 중...");

        QueryResult result = LoginDatabase.Query("SELECT ip, description, max_connections, max_unique_accounts FROM custom_allowed_ips");

        LOG_INFO("module.iplimit", "쿼리 실행 완료, 결과 확인 중...");

        if (!result)
        {
            handler->PSendSysMessage("|cFF00FFFF알림:|r 허용된 IP 목록이 비어있습니다.");
            LOG_INFO("module.iplimit", "쿼리 결과가 비어있음");
            return true;
        }

        LOG_INFO("module.iplimit", "쿼리 결과 존재, 목록 출력 시작");
        handler->PSendSysMessage("|cFF00FF00=== 허용된 IP 목록 ===|r");
        handler->PSendSysMessage("-----------------------------------------------------------------");
        handler->PSendSysMessage("|cFFFFFF00IP 주소           최대접속   최대고유계정   설명|r");
        handler->PSendSysMessage("-----------------------------------------------------------------");

        uint32 count = 0;
        do
        {
            Field* fields = result->Fetch();

            std::string ip = fields[0].Get<std::string>();
            std::string desc = fields[1].Get<std::string>();
            uint32 max_connections = fields[2].Get<uint32>();
            uint32 max_unique_accounts = fields[3].Get<uint32>();

            std::string paddedIp = ip;
            while (paddedIp.length() < 15)
                paddedIp += " ";

            handler->PSendSysMessage("|cFFFFFF00{}|r  |cFFFF0000{:>2}|r         |cFF00FFFF{:>2}|r            {}", paddedIp, max_connections, max_unique_accounts, desc);
            count++;
        } while (result->NextRow());

        handler->PSendSysMessage("-----------------------------------------------------------------");
        handler->PSendSysMessage("총 |cFF00FF00{}|r개의 IP가 등록되어 있습니다.", count);

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

        // 3. DB 초기화 안내
        LOG_INFO("module.iplimit", "IPLimit: 데이터베이스 초기화 중...");
        
        // 테이블 존재 여부 확인
        QueryResult checkTable = LoginDatabase.Query("SHOW TABLES LIKE 'custom_allowed_ips'");
        if (!checkTable)
        {
            // 4. 테이블 생성 안내
            LOG_INFO("module.iplimit", "IPLimit: custom_allowed_ips 테이블을 생성합니다...");
            LoginDatabase.Execute(
                "CREATE TABLE IF NOT EXISTS `custom_allowed_ips` ("
                "`ip` varchar(15) NOT NULL DEFAULT '127.0.0.1',"
                "`description` varchar(255) DEFAULT NULL,"
                "`max_connections` int unsigned NOT NULL DEFAULT 2,"
                "`max_unique_accounts` int unsigned NOT NULL DEFAULT 1,"
                "PRIMARY KEY (`ip`)"
                ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"
            );
            
            // 기본 localhost IP 추가
            LoginDatabase.Execute(
                "INSERT IGNORE INTO custom_allowed_ips (ip, description, max_connections, max_unique_accounts) "
                "VALUES ('127.0.0.1', 'Default localhost', 2, 1)"
            );
            // 5. 테이블 생성 및 기본 데이터 추가 완료
            LOG_INFO("module.iplimit", "IPLimit: 테이블 생성 및 기본 데이터 추가가 완료되었습니다.");
        }

        // 데이터 로드
        QueryResult result = LoginDatabase.Query("SELECT ip, max_connections, max_unique_accounts FROM custom_allowed_ips");
        uint32 count = 0;

        allowedIps.clear(); // 기존 데이터 초기화

        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                std::string ip = fields[0].Get<std::string>();
                uint32 max_connections = fields[1].Get<uint32>();
                uint32 max_unique_accounts = fields[2].Get<uint32>();

                if (!ip.empty() && IsValidIP(ip))
                {
                    allowedIps[ip] = {max_connections, max_unique_accounts};
                    ++count;
                    LOG_DEBUG("module.iplimit", "허용된 IP 로드: {} (최대 접속: {}, 최대 고유 계정: {})", ip, max_connections, max_unique_accounts);
                }
                else
                {
                    LOG_ERROR("module.iplimit", "잘못된 IP 형식 발견: {}", ip);
                }
            } while (result->NextRow());
        }

        // 6. 허용된 IP 로드 완료
        LOG_INFO("module.iplimit", "IPLimit: 데이터베이스에서 {}개의 허용된 IP를 로드했습니다.", count);
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
