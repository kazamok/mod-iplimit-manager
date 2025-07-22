# IP Limit Manager

## 📝 설명
IP Limit Manager는 AzerothCore 서버에서 동일 IP에서의 다중 접속을 제어하고, 계정 생성 시 IP 제한을 관리하는 모듈입니다.
이 모듈은 서버 관리자가 특정 IP 주소에 대한 접속 및 계정 생성을 허용하거나 제한할 수 있게 해줍니다.
안전한 서버 운영을 위한 필수 도구입니다.

## ✨ 주요 기능
- 🔒 **동일 IP에서의 다중 접속 제한 (설정 가능)**
  - `mod-iplimit-manager.conf.dist`의 `IpLimitManager.MaxConnectionsPerIp` 설정을 통해 IP당 허용할 동시 접속 계정 수를 지정할 수 있습니다.
  - 허용된 IP는 제한 없이 접속 가능합니다.
- 🆕 **계정 생성 시 IP 제한**
  - `authserver.conf.dist` 및 `mod-iplimit-manager.conf.dist`의 설정을 통해 특정 IP에서 일정 시간 내에 생성 가능한 계정 수를 제한합니다.
  - `custom_allowed_ips` 테이블의 `can_create_account` 컬럼을 통해 특정 IP의 계정 생성을 명시적으로 허용하거나 차단할 수 있습니다.
- 📈 **IP-계정 사용 기록 추적**
  - `account_ip_usage` 테이블에 계정 로그인 시 사용된 IP 주소, 첫 로그인 시간, 마지막 로그인 시간, 로그인 횟수 등을 기록하여 IP와 계정 간의 관계를 추적할 수 있습니다.
- 📋 **허용된 IP 주소 관리 (추가/삭제/조회)**
  - `.allowip append <ip>` - IP 주소를 허용 목록에 추가
  - `.allowip remove <ip>` - IP 주소를 허용 목록에서 제거
  - `.allowip show` - 허용된 IP 목록 조회
- 📊 **접속 및 계정 생성 로깅 시스템**
  - 모든 접속 시도 및 계정 생성 시도가 CSV 파일로 기록됨
  - 로그 파일명: `access_log_YYYY-MM-DD_HHMMSS.csv`
- 👮 관리자 권한으로 IP 접근 제어
- 💾 데이터베이스 기반 IP 관리

## 📋 요구사항
- AzerothCore v1.0.1 이상
- MySQL 5.7 이상
- ACE ≥ 7.0.0
- C++ 17 지원 컴파일러

## 🚀 설치 방법

1. 모듈을 AzerothCore 소스의 `modules` 디렉토리에 복사합니다.
2. SQL 파일을 **`acore_auth` 데이터베이스**에 임포트합니다. (`mod-iplimit-manager.sql`)
3. CMake를 다시 실행하고 AzerothCore를 새로 빌드합니다.

## ⚙️ 설정 방법 (선택사항)

## 🛡️ 관리자 대처 방안 (계정 생성 IP 제한)

이 모듈의 계정 생성 IP 제한 기능은 스팸 계정 및 봇 생성을 방지하는 데 중점을 둡니다. 관리자는 다음 시나리오를 이해하고 적절히 대처할 수 있습니다.

### 1. 일반적인 계정 생성 (기본 설정)

*   **설정:** `AccountCreationIpLimit.MaxAccountsPerIp = 3`, `AccountCreationIpLimit.TimeframeHours = 24` (기본값)
*   **작동:** 한 IP에서 24시간 내에 최대 3개의 계정을 생성할 수 있습니다. 4번째 계정 생성 시도 시 "계정 생성 제한 초과" 오류가 발생합니다.
*   **관리자 관점:** 이는 의도된 동작이며, 스팸 방지에 효과적입니다.

### 2. 합법적인 사용자가 계정 생성 제한에 걸렸을 때

*   **상황:** 한 IP(예: 가족 공유 IP)에서 합법적인 사용자가 계정 생성 제한에 걸려 추가 계정을 만들 수 없는 경우.
*   **작동:** 계정 생성 시도 시 "계정 생성 제한 초과" 오류가 발생합니다.
*   **관리자 대처:**
    1.  **일시적 해결:** 사용자에게 `AccountCreationIpLimit.TimeframeHours` (기본 24시간) 후에 다시 시도하도록 안내합니다.
    2.  **영구적 해결 (권장):** 해당 IP를 `custom_allowed_ips` 테이블에 추가하고 `can_create_account` 컬럼을 `1`로 설정하여 계정 생성 제한을 우회하도록 합니다.
        *   **인게임 명령어:** `.allowip append <IP주소>` (이 명령어는 `custom_allowed_ips`에 IP를 추가합니다. `can_create_account` 컬럼의 기본값은 `1`입니다.)
        *   **데이터베이스 직접 수정:** `acore_auth` 데이터베이스의 `custom_allowed_ips` 테이블에 직접 `INSERT` 또는 `UPDATE` 쿼리를 실행합니다.
            ```sql
            INSERT INTO custom_allowed_ips (ip, description, can_create_account)
            VALUES ('192.168.1.100', 'Family IP - Account Creation Allowed', 1)
            ON DUPLICATE KEY UPDATE can_create_account = 1;
            ```
            (여기서 '192.168.1.100'은 예시 IP입니다.)

### 3. 스팸/봇 계정 생성이 의심될 때

*   **상황:** 특정 IP에서 비정상적으로 많은 계정 생성 시도가 감지됩니다.
*   **작동:** `account_creation_log` 테이블에 해당 IP의 기록이 쌓이며, `AccountCreationIpLimit.MaxAccountsPerIp`를 초과하면 자동으로 계정 생성이 차단됩니다.
*   **관리자 대처:**
    1.  `acore_auth` 데이터베이스의 `account_creation_log` 테이블을 주기적으로 모니터링하여 의심스러운 IP를 식별합니다.
    2.  필요하다면 해당 IP를 `custom_allowed_ips` 테이블에 추가하고 `can_create_account`를 `0`으로 설정하여 해당 IP의 계정 생성을 명시적으로 차단할 수 있습니다.
        ```sql
        INSERT INTO custom_allowed_ips (ip, description, can_create_account)
        VALUES ('1.2.3.4', 'Spam IP - Account Creation Denied', 0)
        ON DUPLICATE KEY UPDATE can_create_account = 0;
        ```
        (여기서 '1.2.3.4'는 예시 IP입니다.)
    3.  더 강력한 차단이 필요할 경우, 서버 방화벽 또는 다른 보안 솔루션을 통해 해당 IP의 접속 자체를 차단하는 것을 고려할 수 있습니다.

### 4. 계정 생성 제한 기능 자체를 비활성화하고 싶을 때

*   **상황:** 계정 생성에 대한 IP 제한을 전혀 적용하고 싶지 않습니다.
*   **작동:** `EnableAccountCreationIpLimit = 0`으로 설정하면 모든 IP에 대해 계정 생성 제한이 적용되지 않습니다.
*   **관리자 대처:** `mod-iplimit-manager.conf.dist` 파일에서 `EnableAccountCreationIpLimit = 0`으로 변경합니다.

---

1. 로컬 주소 127.0.0.1 은 기본으로 추가되어 있습니다.
2. 모듈 설정을 변경하려면 `configs/modules/mod-iplimit-manager.conf.dist` 및 `authserver.conf.dist` 파일을 편집하세요.
   - `EnableIpLimitManager`: 모듈 활성화 여부 (기본값: 1)
   - `IpLimitManager.Announce.Enable`: 접속 시 알림 메시지 표시 (기본값: 1)
   - `IpLimitManager.MaxConnectionsPerIp`: 특정 IP에서 허용할 최대 동시 접속 계정 수 (기본값: 1)
   - `IpLimitManager.MaxConnectionsPerAllowedIp`: 허용된 IP(custom_allowed_ips에 등록된 IP)에 대해 허용할 최대 동시 접속 계정 수를 설정합니다. 0으로 설정하면 무제한입니다. (기본값: 0)
   - `EnableAccountCreationIpLimit`: 계정 생성 시 IP 제한 모듈 활성화 여부 (기본값: 1)
   - `AccountCreationIpLimit.MaxAccountsPerIp`: 특정 IP에서 일정 시간 내에 생성 가능한 최대 계정 수 (기본값: 3)
   - `AccountCreationIpLimit.TimeframeHours`: 계정 생성 제한을 적용할 시간 범위(시간) (기본값: 24)

## 📊 데이터
- 📌 타입: Server/Player
- 📜 스크립트: IP Limit Manager
- ⚙️ 로깅: CSV 파일 기반
- 💾 SQL: Yes (auth)
  - `custom_allowed_ips` 테이블: 허용된 IP 및 계정 생성 허용 여부 관리
  - `account_creation_log` 테이블: 계정 생성 기록 로깅
  - `account_ip_usage` 테이블: 계정-IP 사용 기록 추적
  - `module_configs` 테이블: 모듈 설정 값 중앙 관리

## 👥 크레딧
- Kazamok
- 모든 기여자들

## 📄 라이선스
이 프로젝트는 GPL-3.0 라이선스 하에 배포됩니다.