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

        // 계정의 IP와 username 가져오기
        if (QueryResult result = LoginDatabase.Query("SELECT username, last_ip FROM account WHERE id = {}", accountId))
        {
            Field* fields = result->Fetch();
            username = fields[0].Get<std::string>();
            ip = fields[1].Get<std::string>();
            
            // account_ip_usage 테이블 업데이트 로직 추가
            LoginDatabase.Execute(
                "INSERT INTO account_ip_usage (account_id, ip_address, first_login_time, last_login_time, login_count) "
                "VALUES ({}, '{}', NOW(), NOW(), 1) "
                "ON DUPLICATE KEY UPDATE last_login_time = NOW(), login_count = login_count + 1",
                accountId, ip
            );

            // CSV 로그 기록
            LogAccountAction(accountId, ip, "login");
        }
        else
        {
            return; // 계정 정보를 찾을 수 없는 경우
        }

        LOG_DEBUG("module.iplimit", "Checking login for account {} (ID: {}) from IP: {}", username, accountId, ip);

        std::lock_guard<std::mutex> lock(ipMutex);

        uint32 maxConnections;
        if (allowedIps.find(ip) != allowedIps.end())
        {
            // 허용된 IP에 대한 최대 연결 수 설정 (0이면 무제한)
            maxConnections = sConfigMgr->GetOption<uint32>("IpLimitManager.MaxConnectionsPerAllowedIp", 0);
            LOG_DEBUG("module.iplimit", "IP {} is in allowed list. Max connections for this IP: {}", ip, maxConnections == 0 ? "Unlimited" : std::to_string(maxConnections));
            if (maxConnections == 0) // 0이면 무제한이므로 바로 리턴
            {
                ipConnectionCount[ip]++; // 무제한이라도 카운트는 증가시켜야 함 (로그아웃 시 감소를 위해)
                return;
            }
        }
        else
        {
            // 일반 IP에 대한 최대 연결 수 설정
            maxConnections = sConfigMgr->GetOption<uint32>("IpLimitManager.MaxConnectionsPerIp", 1);
            LOG_DEBUG("module.iplimit", "IP {} is not in allowed list. Max connections for this IP: {}", ip, maxConnections);
        }

        ipConnectionCount[ip]++;
        LOG_DEBUG("module.iplimit", "IP {} current connection count: {}", ip, ipConnectionCount[ip]);

        // 1. 계정 로그인 시 중복 접속 차단
        if (ipConnectionCount[ip] > maxConnections)
        {
            LOG_INFO("module.iplimit", "IPLimit: 동일한 IP({})에서 이미 다른 계정이 접속 중이므로 계정 ({})이 차단됩니다.", ip, username);
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
                    ChatHandler(player->GetSession()).PSendSysMessage("|cff4CFF00[IP Limit Manager]|r 이 서버는 IP 제한 모듈이 실행 중입니다.");
                }
                // return; // 이 줄을 제거합니다.
            }
        }

        // 이미 접속중인 다른 계정이 있는지 확인
        QueryResult result = CharacterDatabase.Query(
            "SELECT c.online, c.name, c.account "
            "FROM characters c "
            "INNER JOIN acore_auth.account a ON c.account = a.id "
            "WHERE a.last_ip = '{}' AND c.online = 1 AND c.account != {}",
            playerIp, accountId);

        // 2. 플레이어 로그인 시 중복 접속 감지 및 퇴장 예약
        if (result)
        {
            Field* fields = result->Fetch();
            std::string existingCharName = fields[1].Get<std::string>();
            
            LOG_INFO("module.iplimit", "IPLimit: 동일한 IP({})에서 이미 다른 캐릭터가 접속 중이므로 캐릭터 ({})가 30초 후 강제 퇴장이 예약됩니다.", playerIp, player->GetName());

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
            ChatHandler(player->GetSession()).PSendSysMessage("|cff4CFF00[IP Limit Manager]|r 경고: 다른 캐릭터가 같은 IP 주소에서 이미 접속 중입니다. 30초 후 연결이 끊어집니다.");
        }
        else if (sConfigMgr->GetOption<bool>("IpLimitManager.Announce.Enable", false))
        {
            ChatHandler(player->GetSession()).PSendSysMessage("|cff4CFF00[IP Limit Manager]|r 이 서버는 IP 제한 모듈이 실행되고 있습니다.");
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
                ChatHandler(player->GetSession()).PSendSysMessage("|cff4CFF00[IP Limit Manager]|r 경고: 10초 후에 연결이 끊어집니다.");
                it->second.messageSent = true;
            }

            // 시간이 다 되면 강제 퇴장
            if (remainingTime == 0)
            {
                ChatHandler(player->GetSession()).PSendSysMessage("|cff4CFF00[IP Limit Manager]|r IP 제한으로 인해 연결이 끊어졌습니다.");
                player->GetSession()->KickPlayer();
                
                // 데이터베이스에서 온라인 상태 해제
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
            handler->PSendSysMessage("사용법: .allowip append <ip>");
            handler->PSendSysMessage("예시: .allowip append 192.168.1.1");
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

        QueryResult result = LoginDatabase.Query("SELECT ip, description FROM custom_allowed_ips");

        LOG_INFO("module.iplimit", "쿼리 실행 완료, 결과 확인 중...");

        // 쿼리 실패 시 처리
        if (!result)
        {
            handler->PSendSysMessage("|cFF00FFFF알림:|r 허용된 IP 목록이 비어있습니다.");
            LOG_INFO("module.iplimit", "쿼리 결과가 비어있음");
            return true;
        }

        LOG_INFO("module.iplimit", "쿼리 결과 존재, 목록 출력 시작");
        handler->PSendSysMessage("|cFF00FF00=== 허용된 IP 목록 ===|r");
        handler->PSendSysMessage("----------------------------------------");
        handler->PSendSysMessage("|cFFFFFF00IP 주소           설명|r");
        handler->PSendSysMessage("----------------------------------------");

        uint32 count = 0;
        do
        {
            Field* fields = result->Fetch();

            std::string ip = fields[0].Get<std::string>();
            std::string desc = fields[1].Get<std::string>();

            std::string paddedIp = ip;
            while (paddedIp.length() < 15)
                paddedIp += " ";

            handler->PSendSysMessage("|cFFFFFF00{}|r  {}", paddedIp, desc);
            count++;
        } while (result->NextRow());

        handler->PSendSysMessage("----------------------------------------");
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
        
        // custom_allowed_ips 테이블 존재 여부 확인 및 생성
        QueryResult checkAllowedIpsTable = LoginDatabase.Query("SHOW TABLES LIKE 'custom_allowed_ips'");
        if (!checkAllowedIpsTable)
        {
            LOG_INFO("module.iplimit", "IPLimit: custom_allowed_ips 테이블을 생성합니다...");
            LoginDatabase.Execute(
                "CREATE TABLE IF NOT EXISTS `custom_allowed_ips` ("
                "`ip` varchar(15) NOT NULL DEFAULT '127.0.0.1',"
                "`description` varchar(255) DEFAULT NULL,"
                "`can_create_account` tinyint(1) NOT NULL DEFAULT '1',"
                "PRIMARY KEY (`ip`)"
                ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"
            );
            
            LoginDatabase.Execute(
                "INSERT IGNORE INTO custom_allowed_ips (ip, description, can_create_account) "
                "VALUES ('127.0.0.1', 'Default localhost', 1)"
            );
            LOG_INFO("module.iplimit", "IPLimit: custom_allowed_ips 테이블 생성 및 기본 데이터 추가 완료.");
        }
        else
        {
            // custom_allowed_ips 테이블에 can_create_account 컬럼이 없는 경우 추가
            QueryResult checkColumn = LoginDatabase.Query("SHOW COLUMNS FROM `custom_allowed_ips` LIKE 'can_create_account'");
            if (!checkColumn)
            {
                LOG_INFO("module.iplimit", "IPLimit: custom_allowed_ips 테이블에 'can_create_account' 컬럼을 추가합니다...");
                LoginDatabase.Execute("ALTER TABLE `custom_allowed_ips` ADD COLUMN `can_create_account` tinyint(1) NOT NULL DEFAULT '1'");
                LOG_INFO("module.iplimit", "IPLimit: 'can_create_account' 컬럼 추가 완료.");
            }
        }

        // account_creation_log 테이블 존재 여부 확인 및 생성
        QueryResult checkCreationLogTable = LoginDatabase.Query("SHOW TABLES LIKE 'account_creation_log'");
        if (!checkCreationLogTable)
        {
            LOG_INFO("module.iplimit", "IPLimit: account_creation_log 테이블을 생성합니다...");
            LoginDatabase.Execute(
                "CREATE TABLE IF NOT EXISTS `account_creation_log` ("
                "`ip` varchar(15) NOT NULL,"
                "`creation_time` datetime NOT NULL,"
                "`account_id` int(10) unsigned NOT NULL,"
                "`account_username` varchar(50) NOT NULL,"
                "PRIMARY KEY (`ip`, `creation_time`)"
                ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"
            );
            LOG_INFO("module.iplimit", "IPLimit: account_creation_log 테이블 생성 완료.");
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

namespace mod_iplimit_manager
{
    bool IsAccountCreationAllowed(const std::string& ipAddress, uint32& currentCreations)
    {
        // EnableAccountCreationIpLimit 설정 확인
        if (!sConfigMgr->GetOption<bool>("EnableAccountCreationIpLimit", true))
        {
            LOG_DEBUG("module.iplimit", "Account creation IP limit disabled in config.");
            return true; // 설정에서 비활성화된 경우 항상 허용
        }

        // custom_allowed_ips 테이블에서 IP 조회 (생성 제한을 우회할 수 있는 IP인지 확인)
        bool canCreate = false;
        if (QueryResult allowedResult = LoginDatabase.Query("SELECT can_create_account FROM custom_allowed_ips WHERE ip = '{}'", ipAddress))
        {
            if (allowedResult->Fetch()[0].Get<bool>())
            {
                canCreate = true; // custom_allowed_ips에 있고 can_create_account가 true이면 생성 허용
            }
        }

        if (canCreate)
        {
            LOG_DEBUG("module.iplimit", "IP {} is in allowed list for account creation. Skipping restriction.", ipAddress);
            return true; // 허용된 IP는 제한 없이 생성 허용
        }

        // AccountCreationIpLimit.TimeframeHours 설정 값 가져오기
        uint32 timeframeHours = sConfigMgr->GetOption<uint32>("AccountCreationIpLimit.TimeframeHours", 24);

        // 24시간 이내 해당 IP에서 생성된 계정 수 확인
        uint32 creationCount = 0;
        if (QueryResult creationResult = LoginDatabase.Query("SELECT COUNT(*) FROM account_creation_log WHERE ip = '{}' AND creation_time >= DATE_SUB(NOW(), INTERVAL {} HOUR)", ipAddress, timeframeHours))
        {
            creationCount = creationResult->Fetch()[0].Get<uint32>();
        }
        currentCreations = creationCount; // 현재 생성된 계정 수 반환

        // AccountCreationIpLimit.MaxAccountsPerIp 설정 값 가져오기
        uint32 maxAccounts = sConfigMgr->GetOption<uint32>("AccountCreationIpLimit.MaxAccountsPerIp", 3);

        // 생성 제한 (설정된 최대 계정 수 초과 여부 확인)
        if (creationCount >= maxAccounts)
        {
            LOG_INFO("module.iplimit", "Account creation from IP {} denied. {} accounts created in last {} hours (max {} allowed).", ipAddress, creationCount, timeframeHours, maxAccounts);
            return false; // 생성 제한 초과
        }

        return true; // 생성 허용
    }
}
