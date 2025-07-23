# IP Limit Manager

## 📝 설명
IP Limit Manager는 AzerothCore 서버에서 동일 IP에서의 다중 접속을 제어하고, 계정 생성 시 IP 제한을 관리하는 모듈입니다.
이 모듈은 서버 관리자가 특정 IP 주소에 대한 접속 및 계정 생성을 허용하거나 제한할 수 있게 해줍니다.
안전한 서버 운영을 위한 필수 도구입니다.

## ✨ 주요 기능
- 🔒 **동일 IP에서의 다중 접속 제한 (설정 가능)**
  - `mod-iplimit-manager.conf.dist`의 `IpLimitManager.MaxConnectionsPerIp` 설정을 통해 IP당 허용할 동시 접속 계정 수를 지정할 수 있습니다.
  - 허용된 IP(`ip_login_allowlist` 테이블에 등록)는 제한을 우회할 수 있습니다.
- 🆕 **계정 생성 시 IP 제한**
  - `mod-iplimit-manager.conf.dist`의 설정을 통해 특정 IP에서 일정 시간 내에 생성 가능한 계정 수를 제한합니다.
  - `ip_registration_rules` 테이블의 `can_create_account` 컬럼을 통해 특정 IP의 계정 생성을 명시적으로 허용하거나 차단할 수 있습니다.
- 📈 **IP-계정 사용 기록 추적**
  - `account_ip_usage` 테이블에 계정 로그인 시 사용된 IP 주소, 첫 로그인 시간, 마지막 로그인 시간, 로그인 횟수 등을 기록하여 IP와 계정 간의 관계를 추적할 수 있습니다.
- 📋 **허용된 IP 주소 관리 (추가/삭제/조회)**
  - **로그인 허용 목록 관리:**
    - `.allowip login add <ip> [설명]` - IP를 로그인 허용 목록에 추가
    - `.allowip login remove <ip>` - IP를 로그인 허용 목록에서 제거
    - `.allowip login show` - 로그인 허용 목록 조회
  - **계정 생성 규칙 관리:**
    - `.allowip register add <ip> [설명]` - IP를 계정 생성 규칙에 추가 (기본적으로 생성 허용)
    - `.allowip register remove <ip>` - IP를 계정 생성 규칙에서 제거
    - `.allowip register show` - 계정 생성 규칙 목록 조회
- 📊 **접속 및 계정 생성 로깅 시스템**
  - 모든 접속 시도 및 계정 생성 시도가 CSV 파일로 기록됨
  - 로그 파일명: `access_log_YYYY-MM-DD_HHMMSS.csv`
- 👮 관리자 권한으로 IP 접근 제어
- 💾 데이터베이스 기반 IP 관리

## 📋 요구사항
- AzerothCore v1.0.1 이상
- MySQL 5.7 이상
- C++ 17 지원 컴파일러

## 🚀 설치 방법

1.  모듈을 AzerothCore 소스의 `modules` 디렉토리에 복사합니다.
2.  CMake를 다시 실행하고 AzerothCore를 새로 빌드합니다.
3.  서버를 처음 시작할 때, 모듈이 `acore_auth` 데이터베이스에 필요한 테이블(`ip_login_allowlist`, `ip_registration_rules`, `account_creation_log`, `account_ip_usage`)을 **자동으로 생성**합니다. 별도의 `.sql` 파일을 임포트할 필요가 없습니다.

## 🛡️ 관리자 대처 방안 (계정 생성 IP 제한)

이 모듈의 계정 생성 IP 제한 기능은 스팸 계정 및 봇 생성을 방지하는 데 중점을 둡니다. 관리자는 다음 시나리오를 이해하고 적절히 대처할 수 있습니다.

### 1. 일반적인 계정 생성 (기본 설정)

*   **설정:** `AccountCreationIpLimit.MaxAccountsPerIp = 3`, `AccountCreationIpLimit.TimeframeHours = 24` (기본값)
*   **작동:** 한 IP에서 24시간 내에 최대 3개의 계정을 생성할 수 있습니다. 4번째 계정 생성 시도 시 "계정 생성 제한 초과" 오류가 발생합니다.
*   **관리자 관점:** 이는 의도된 동작이며, 스팸 방지에 효과적입니다.

### 2. 합법적인 사용자가 계정 생성 제한에 걸렸을 때

*   **상황:** 한 IP(예: 가족 공유 IP)에서 합법적인 사용자가 계정 생성 제한에 걸려 추가 계정을 만들 수 없는 경우.
*   **관리자 대처:**
    1.  **일시적 해결:** 사용자에게 `AccountCreationIpLimit.TimeframeHours` (기본 24시간) 후에 다시 시도하도록 안내합니다.
    2.  **영구적 해결 (권장):** 해당 IP를 `ip_registration_rules` 테이블에 추가하여 계정 생성 제한을 우회하도록 합니다.
        *   **인게임 명령어:** `.allowip register add <IP주소> [설명]` (예: `.allowip register add 192.168.1.100 가족 공유 IP`)
        *   **데이터베이스 직접 수정:** `acore_auth` 데이터베이스의 `ip_registration_rules` 테이블에 직접 `INSERT` 쿼리를 실행합니다.
            ```sql
            INSERT INTO ip_registration_rules (ip, description, can_create_account)
            VALUES ('192.168.1.100', 'Family IP - Account Creation Allowed', 1)
            ON DUPLICATE KEY UPDATE can_create_account = 1;
            ```

### 3. 스팸/봇 계정 생성이 의심될 때

*   **상황:** 특정 IP에서 비정상적으로 많은 계정 생성 시도가 감지됩니다.
*   **관리자 대처:**
    1.  `acore_auth` 데이터베이스의 `account_creation_log` 테이블을 주기적으로 모니터링하여 의심스러운 IP를 식별합니다.
    2.  필요하다면 해당 IP의 계정 생성을 명시적으로 차단할 수 있습니다.
        *   **데이터베이스 직접 수정:** `ip_registration_rules` 테이블에 IP를 추가하고 `can_create_account`를 `0`으로 설정합니다.
            ```sql
            INSERT INTO ip_registration_rules (ip, description, can_create_account)
            VALUES ('1.2.3.4', 'Spam IP - Account Creation Denied', 0)
            ON DUPLICATE KEY UPDATE can_create_account = 0;
            ```

### 4. 계정 생성 제한 기능 자체를 비활성화하고 싶을 때

*   **관리자 대처:** `mod-iplimit-manager.conf.dist` 파일에서 `EnableAccountCreationIpLimit = 0`으로 변경합니다.

---

1.  로컬 주소 `127.0.0.1`은 기본적으로 로그인 허용 목록과 계정 생성 규칙에 추가되어 있습니다.
2.  모듈 설정을 변경하려면 `conf/mod-iplimit-manager.conf.dist` 파일을 수정한 후, 빌드 디렉토리의 `configs` 폴더에 `mod-iplimit-manager.conf`로 복사하세요.
   - `EnableIpLimitManager`: 모듈 활성화 여부 (기본값: 1)
   - `IpLimitManager.Announce.Enable`: 접속 시 알림 메시지 표시 (기본값: 1)
   - `IpLimitManager.MaxConnectionsPerIp`: 특정 IP에서 허용할 최대 동시 접속 계정 수 (기본값: 1)
   - `IpLimitManager.MaxConnectionsPerAllowedIp`: 허용된 IP(`ip_login_allowlist`에 등록된 IP)에 대해 허용할 최대 동시 접속 계정 수를 설정합니다. 0으로 설정하면 무제한입니다. (기본값: 0)
   - `EnableAccountCreationIpLimit`: 계정 생성 시 IP 제한 모듈 활성화 여부 (기본값: 1)
   - `AccountCreationIpLimit.MaxAccountsPerIp`: 특정 IP에서 일정 시간 내에 생성 가능한 최대 계정 수 (기본값: 3)
   - `AccountCreationIpLimit.TimeframeHours`: 계정 생성 제한을 적용할 시간 범위(시간) (기본값: 24)

## 📊 데이터
- 📌 타입: Server/Player
- 📜 스크립트: IP Limit Manager
- ⚙️ 로깅: CSV 파일 기반 (`logs/iplimit/` 폴더에 저장)
- 💾 SQL: Yes (acore_auth 데이터베이스)
  - `ip_login_allowlist`: 다중 접속 제한을 우회할 IP 목록
  - `ip_registration_rules`: 계정 생성 제한 규칙을 관리할 IP 목록
  - `account_creation_log`: 계정 생성 기록 로깅
  - `account_ip_usage`: 계정-IP 사용 기록 추적

## 👥 크레딧
- Kazamok
- 모든 기여자들

## 📄 라이선스
이 프로젝트는 GPL-3.0 라이선스 하에 배포됩니다.
