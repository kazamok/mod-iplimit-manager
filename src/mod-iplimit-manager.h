

/* filename:
mod-iplimit-manager.h */

#ifndef MOD_IPLIMIT_MANAGER_H
#define MOD_IPLIMIT_MANAGER_H

#include "Define.h"
#include <string>

namespace mod_iplimit_manager
{
    // 계정 생성 제한 여부를 확인하는 함수
    // ipAddress: 계정 생성 요청 IP 주소
    // currentCreations: (출력) 해당 IP에서 현재까지 생성된 계정 수
    // 반환값: 계정 생성이 허용되면 true, 아니면 false
    bool IsAccountCreationAllowed(const std::string& ipAddress, uint32& currentCreations);
}

#endif // MOD_IPLIMIT_MANAGER_H
