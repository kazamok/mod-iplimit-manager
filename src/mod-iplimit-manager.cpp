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

// 한국어 주석: IP 연결 제한 관리자 모듈
// 이 모듈은 동일한 IP에서 여러 클라이언트 연결을 제한합니다.
// 기본적으로 한 IP당 하나의 연결만 허용합니다.
// 예외 IP는 명령어로 추가하거나 제거할 수 있습니다.

std::mutex ipMutex;
std::unordered_map<std::string, uint32> ipConnectionCount;
std::set<std::string> allowedIps;

class IpLimitManager_PlayerScript : public PlayerScript
{
public:
    IpLimitManager_PlayerScript() : PlayerScript("IpLimitManager_PlayerScript") {}

    // 한국어 주석: 플레이어 로그인 시 IP 제한 확인
    void OnLogin(Player* player)
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

    // 한국어 주석: 플레이어 로그아웃 시 IP 연결 수 감소
    void OnLogout(Player* player)
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

    // 한국어 주석: IP 추가 명령어 처리
    static bool HandleAddIpCommand(ChatHandler* handler, std::string const& args)
    {
        if (args.empty())
            return false;

        std::string ip = args;
        QueryResult result = WorldDatabase.Query("REPLACE INTO custom_allowed_ips (ip) VALUES ('%s')", ip.c_str());
        allowedIps.insert(ip);
        handler->SendSysMessage("Added to allowed IP list.");
        return true;
    }

    // 한국어 주석: IP 제거 명령어 처리
    static bool HandleDelIpCommand(ChatHandler* handler, std::string const& args)
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

// 한국어 주석: DB에서 허용된 IP 목록 로드
void LoadAllowedIpsFromDB()
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

// 한국어 주석: 모듈 스크립트 등록
void Addmod_iplimit_managerScripts()
{
    LoadAllowedIpsFromDB();
    new IpLimitManager_PlayerScript();
    new IpLimitManager_CommandScript();
}
