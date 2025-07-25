

/* filename:
mod-iplimit-manager.cpp */



#include <cstdint>          // Explicitly for uint32_t

#include "Define.h"         // For uint32 and other basic types
#include "SharedDefines.h"  // For uint32 and other basic types
#include "Common.h"         // General utilities, often pulls in many core headers
#include "Config.h"         // For sConfigMgr
#include "Log.h"            // For LOG_INFO, LOG_DEBUG, LOG_ERROR
#include "ObjectGuid.h"     // For ObjectGuid

#include "Player.h"
#include "World.h"
#include "ScriptMgr.h"
#include "Chat.h"
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

        static bool (*HandleLoginAddIpCommandPtr)(ChatHandler*, std::string_view) = HandleLoginAddIpCommand;
        static bool (*HandleLoginDelIpCommandPtr)(ChatHandler*, std::string_view) = HandleLoginDelIpCommand;
        static bool (*HandleLoginShowIpCommandPtr)(ChatHandler*, std::string_view) = HandleLoginShowIpCommand;

        static ChatCommandTable allowIpLoginSubCommandTable =
        {
            ChatCommandBuilder("add",    *HandleLoginAddIpCommandPtr,    SEC_ADMINISTRATOR, Console::Yes ),
            ChatCommandBuilder("remove", *HandleLoginDelIpCommandPtr,    SEC_ADMINISTRATOR, Console::Yes ),
            ChatCommandBuilder("show",   *HandleLoginShowIpCommandPtr,   SEC_ADMINISTRATOR, Console::Yes )
        };

        static bool (*HandleRegisterAddIpCommandPtr)(ChatHandler*, std::string_view) = HandleRegisterAddIpCommand;
        static bool (*HandleRegisterDelIpCommandPtr)(ChatHandler*, std::string_view) = HandleRegisterDelIpCommand;
        static bool (*HandleRegisterShowIpCommandPtr)(ChatHandler*, std::string_view) = HandleRegisterShowIpCommand;

        static ChatCommandTable allowIpRegisterSubCommandTable =
        {
            ChatCommandBuilder("add",    *HandleRegisterAddIpCommandPtr, SEC_ADMINISTRATOR, Console::Yes ),
            ChatCommandBuilder("remove", *HandleRegisterDelIpCommandPtr, SEC_ADMINISTRATOR, Console::Yes ),
            ChatCommandBuilder("show",   *HandleRegisterShowIpCommandPtr,SEC_ADMINISTRATOR, Console::Yes )
        };

        static const std::vector<Acore::ChatCommands::ChatCommandBuilder> allowIpSubCommands =
        {
            ChatCommandBuilder("login", allowIpLoginSubCommandTable),
            ChatCommandBuilder("register", allowIpRegisterSubCommandTable)
        };

        static ChatCommandTable commandTable =
        {
            ChatCommandBuilder("allowip", allowIpSubCommands)
        };
 
        return commandTable;
    }

private:
    // --- Helper function to get table name and validate ---
    static std::string GetTableName(const std::string& type, ChatHandler* handler)
    {
        if (type == "login")
        {
            return "ip_login_allowlist";
        }
        if (type == "register")
        {
            return "ip_registration_rules";
        }
        handler->PSendSysMessage("Invalid command type specified.");
        return "";
    }

    // --- Login IP Commands ---
    static bool HandleLoginAddIpCommand(ChatHandler* handler, std::string_view args)
    { 
        return HandleAddIpCommand(handler, args, "login");
    }

    static bool HandleLoginDelIpCommand(ChatHandler* handler, std::string_view args)
    { 
        return HandleDelIpCommand(handler, args, "login");
    }

    static bool HandleLoginShowIpCommand(ChatHandler* handler, std::string_view args)
    { 
        return HandleShowIpCommand(handler, args, "login");
    }

    // --- Register IP Commands ---
    static bool HandleRegisterAddIpCommand(ChatHandler* handler, std::string_view args)
    { 
        return HandleAddIpCommand(handler, args, "register");
    }

    static bool HandleRegisterDelIpCommand(ChatHandler* handler, std::string_view args)
    { 
        return HandleDelIpCommand(handler, args, "register");
    }

    static bool HandleRegisterShowIpCommand(ChatHandler* handler, std::string_view args)
    { 
        return HandleShowIpCommand(handler, args, "register");
    }

    // --- Generic Command Handlers ---
    static bool HandleAddIpCommand(ChatHandler* handler, std::string_view args, const std::string& type)
    {
        std::string tableName = GetTableName(type, handler);
        if (tableName.empty()) return false;

        if (args.empty())
        {
            handler->PSendSysMessage("Usage: .allowip {} add <ip> [description]", type);
            return false;
        }

        std::string args_str(args);
        std::stringstream ss(args_str);
        std::string ip;
        std::string description;
        ss >> ip;
        std::getline(ss >> std::ws, description);

        if (!IsValidIP(ip))
        {
            handler->PSendSysMessage("Error: Invalid IP address format.");
            return false;
        }

        QueryResult checkResult = LoginDatabase.Query("SELECT 1 FROM {} WHERE ip = '{}'", tableName, ip);
        if (checkResult)
        {
            handler->PSendSysMessage("Error: IP {} already exists in the {} list.", ip, type);
            return false;
        }

        if (type == "register") {
            LoginDatabase.Execute("INSERT INTO {} (ip, description, can_create_account) VALUES ('{}', '{}', 1)", tableName, ip, description);
        } else {
            LoginDatabase.Execute("INSERT INTO {} (ip, description) VALUES ('{}', '{}')", tableName, ip, description);
        }

        if (type == "login") {
            std::lock_guard<std::mutex> lock(ipMutex);
            allowedIps.insert(ip);
        }

        handler->PSendSysMessage("IP {} has been added to the {} allow list.", ip, type);
        return true;
    }

    static bool HandleDelIpCommand(ChatHandler* handler, std::string_view args, const std::string& type)
    {
        std::string tableName = GetTableName(type, handler);
        if (tableName.empty()) return false;

        if (args.empty())
        {
            handler->PSendSysMessage("Usage: .allowip {} remove <ip>", type);
            return false;
        }

        std::string ip(args);
        if (!IsValidIP(ip))
        {
            handler->PSendSysMessage("Error: Invalid IP address format.");
            return false;
        }

        QueryResult checkResult = LoginDatabase.Query("SELECT 1 FROM {} WHERE ip = '{}'", tableName, ip);
        if (!checkResult)
        {
            handler->PSendSysMessage("Error: IP {} does not exist in the {} list.", ip, type);
            return false;
        }

        LoginDatabase.Execute("DELETE FROM {} WHERE ip = '{}'", tableName, ip);
        
        if (type == "login") {
            std::lock_guard<std::mutex> lock(ipMutex);
            allowedIps.erase(ip);
        }

        handler->PSendSysMessage("IP {} has been removed from the {} allow list.", ip, type);
        return true;
    }

    static bool HandleShowIpCommand(ChatHandler* handler, std::string_view args, const std::string& type)
    {
        std::string tableName = GetTableName(type, handler);
        if (tableName.empty()) return false;

        QueryResult result = LoginDatabase.Query("SELECT * FROM {}", tableName);

        if (!result)
        {
            handler->PSendSysMessage("The {} allow list is empty.", type);
            return true;
        }

        handler->PSendSysMessage("--- Allowed {} IPs ---", type);
        do
        {
            Field* fields = result->Fetch();
            std::string ip = fields[0].Get<std::string>();
            std::string desc = fields[1].Get<std::string>();
            if (type == "register") {
                bool canCreate = fields[2].Get<bool>();
                handler->PSendSysMessage("IP: {}, Can Create: {}, Desc: {}", ip, canCreate ? "Yes" : "No", desc);
            } else {
                handler->PSendSysMessage("IP: {}, Desc: {}", ip, desc);
            }
        } while (result->NextRow());
        handler->PSendSysMessage("-----------------------");

        return true;
    }
};

void LoadAllowedIpsFromDB()
{
    try
    {
        // --- 1. 데이터베이스 연결 확인 ---
        QueryResult testConnection = LoginDatabase.Query("SELECT 1");
        if (!testConnection)
        {
            LOG_ERROR("module.iplimit", "DB connection is not available. Cannot load IP rules.");
            return;
        }

        LOG_INFO("module.iplimit", "IPLimit: Initializing database tables...");

        // --- 2. 로그인 허용 목록 테이블 (ip_login_allowlist) 생성 ---
        LOG_INFO("module.iplimit", "IPLimit: Ensuring 'ip_login_allowlist' table exists...");
        LoginDatabase.Execute(
            "CREATE TABLE IF NOT EXISTS `ip_login_allowlist` ("
            "`ip` varchar(15) NOT NULL,"
            "`description` varchar(255) DEFAULT NULL,"
            "PRIMARY KEY (`ip`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"
        );
        // 기본 로컬호스트 추가 (이미 존재하면 무시)
        LoginDatabase.Execute("INSERT IGNORE INTO `ip_login_allowlist` (ip, description) VALUES ('127.0.0.1', 'Default localhost')");
        LOG_INFO("module.iplimit", "IPLimit: 'ip_login_allowlist' table ensured.");

        // --- 3. 계정 생성 규칙 테이블 (ip_registration_rules) 생성 ---
        LOG_INFO("module.iplimit", "IPLimit: Ensuring 'ip_registration_rules' table exists...");
        LoginDatabase.Execute(
            "CREATE TABLE IF NOT EXISTS `ip_registration_rules` ("
            "`ip` varchar(15) NOT NULL,"
            "`description` varchar(255) DEFAULT NULL,"
            "`can_create_account` tinyint(1) NOT NULL DEFAULT '1',"
            "PRIMARY KEY (`ip`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"
        );
        // 기본 로컬호스트 추가 (이미 존재하면 무시)
        LoginDatabase.Execute("INSERT IGNORE INTO `ip_registration_rules` (ip, description, can_create_account) VALUES ('127.0.0.1', 'Default localhost', 1)");
        LOG_INFO("module.iplimit", "IPLimit: 'ip_registration_rules' table ensured.");

        // --- 4. 계정 생성 로그 테이블 (account_creation_log) 생성 ---
        LOG_INFO("module.iplimit", "IPLimit: Ensuring 'account_creation_log' table exists...");
        LoginDatabase.Execute(
            "CREATE TABLE IF NOT EXISTS `account_creation_log` ("
            "`ip` varchar(15) NOT NULL,"
            "`creation_time` datetime NOT NULL,"
            "`account_id` int(10) unsigned NOT NULL,"
            "`account_username` varchar(50) NOT NULL,"
            "PRIMARY KEY (`ip`, `creation_time`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"
        );
        LOG_INFO("module.iplimit", "IPLimit: 'account_creation_log' table ensured.");

        // --- 5. 로그인 허용 IP 목록 메모리 로드 ---
        QueryResult result = LoginDatabase.Query("SELECT ip FROM ip_login_allowlist");
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
                    LOG_DEBUG("module.iplimit", "Loaded allowed login IP: {}", ip);
                }
                else
                {
                    LOG_ERROR("module.iplimit", "Invalid IP format found in ip_login_allowlist: {}", ip);
                }
            } while (result->NextRow());
        }

        LOG_INFO("module.iplimit", "IPLimit: Loaded {} allowed login IPs from the database.", count);
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("module.iplimit", "Error while loading IP rules from DB: {}", e.what());
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
        if (QueryResult allowedResult = LoginDatabase.Query("SELECT can_create_account FROM ip_registration_rules WHERE ip = '{}'", ipAddress))
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
