# IP Limit Manager

## 📝 설명
**IP Limit Manager**는 AzerothCore 서버에서 IP 주소를 기반으로 클라이언트의 접속을 정교하게 제어하는 모듈입니다.

이 모듈은 서로 독립적으로 작동하는 **'동시 접속 제한'**과 **'로그인 빈도 제한'**이라는 두 가지 핵심 기능을 통해 서버의 보안과 안정성을 강화합니다. 제한 정책에 위반된 플레이어는 즉시 접속이 종료되지 않고, 명확한 사유와 함께 30초의 유예 시간을 부여됩니다.

관리자는 설정 파일과 데이터베이스를 연동하여 기본 정책과 개별적인 예외 정책(화이트리스트)을 손쉽게 관리할 수 있습니다.

## ✨ 주요 기능
- **독립적인 듀얼 제한 시스템:**
  - 🔒 **동시 접속 제한:** 하나의 IP에서 동시에 접속할 수 있는 최대 계정 수를 제한합니다. (`IpLimitManager.Max.Account.Enable`로 제어)
  - ⏱️ **로그인 빈도 제한:** 일정 시간 내에 하나의 IP에서 로그인할 수 있는 고유 계정의 수를 제한하여 작업장 등의 비정상적인 접속 패턴을 차단합니다. (`IpLimitManager.RateLimit.Enable`로 제어)
- **유연한 정책 관리:**
  - ⚙️ **기본 정책:** 설정 파일에서 서버 전체에 적용될 기본 제한 규칙을 설정합니다.
  - 📋 **개별 정책 (화이트리스트):** `acore_auth` 데이터베이스의 `custom_allowed_ips` 테이블에 IP를 등록하여, 특정 IP에만 적용되는 개별적인 제한 규칙을 설정할 수 있습니다.
- **개선된 사용자 경험:**
  - ⚠️ **명확한 안내:** 제한에 걸리면, "동시 접속 초과" 또는 "로그인 빈도 초과"와 같이 명확한 사유가 담긴 경고 메시지가 게임 내에 표시됩니다.
  - ⏳ **유예 시간 부여:** 경고 메시지와 함께 30초의 유예 시간이 주어지며, 시간이 다 되면 접속이 종료됩니다.
- **인게임 명령어:**
  - 관리자는 게임 내에서 `.allowip` 명령어를 사용하여 화이트리스트를 실시간으로 추가, 삭제, 조회할 수 있습니다.
- **상세 로깅:**
  - 모든 계정의 로그인/로그아웃 활동이 `logs/iplimit/` 폴더에 날짜와 시간별 CSV 파일로 기록되어 추적이 용이합니다.

## 🚀 설치 방법
1.  이 모듈 폴더를 AzerothCore 소스 트리의 `modules` 디렉토리에 복사합니다.
2.  `data/sql/db-auth/mod-iplimit-manager.sql` 파일을 `acore_auth` 데이터베이스에 임포트(import)합니다.
3.  CMake를 다시 실행하고 AzerothCore를 새로 빌드합니다.

## ⚙️ 설정 및 사용법

### 1. 기본 정책 설정 (conf 파일)
`worldserver` 실행 파일이 있는 폴더에 `mod-iplimit-manager.conf` 파일을 두고 아래 값을 수정하여 서버의 기본 정책을 설정합니다. 이 설정은 **화이트리스트에 없는 모든 IP**에 적용됩니다. (최초에는 `mod-iplimit-manager.conf.dist` 파일의 이름을 변경하여 사용하세요.)

- `EnableIpLimitManager`: 모듈 전체를 활성화합니다. (기본값: 1)
- `IpLimitManager.Announce.Enable`: 접속 시 활성화된 제한 정책을 알리는 메시지를 표시합니다. (기본값: 1)

**[동시 접속 제한]**
- `IpLimitManager.Max.Account.Enable`: 이 기능을 켜거나 끕니다. (기본값: 1)
- `IpLimitManager.Max.Account`: 허용할 **최대 동시 접속** 계정 수를 설정합니다. (기본값: 1)

**[로그인 빈도 제한]**
- `IpLimitManager.RateLimit.Enable`: 이 기능을 켜거나 끕니다. (기본값: 1)
- `IpLimitManager.RateLimit.TimeWindowSeconds`: 고유 계정 수를 체크할 시간 범위(초)를 설정합니다. (기본값: 3600)
- `IpLimitManager.RateLimit.MaxUniqueAccounts`: 위 시간 동안 허용할 **최대 고유 계정** 수를 설정합니다. (기본값: 1)

### 2. 개별 정책 설정 (화이트리스트)
특정 IP에 대해 기본 정책과 다른 규칙을 적용하려면 `acore_auth.custom_allowed_ips` 테이블에 직접 추가하거나 아래의 인게임 명령어를 사용합니다.

#### 데이터베이스 테이블 구조
- `ip` (VARCHAR): 대상 IP 주소
- `description` (VARCHAR): 설명
- `max_connections` (INT): 해당 IP에 허용할 **최대 동시 접속** 수
- `max_unique_accounts` (INT): 해당 IP에 허용할 **최대 고유 계정** 수

#### 인게임 명령어
- `.allowip append <ip> [max_conn] [max_unique]`
  - 화이트리스트에 IP를 추가합니다. `max_conn`과 `max_unique` 값을 생략하면 각각 2와 1이 기본값으로 설정됩니다.
  - 예시: `.allowip append 1.2.3.4 3 5` (1.2.3.4 IP에 동시접속 3개, 시간당 고유계정 5개 허용)
- `.allowip remove <ip>`
  - 화이트리스트에서 IP를 제거합니다.
- `.allowip show`
  - 화이트리스트에 등록된 모든 IP와 설정된 제한 값을 보여줍니다.

## 👥 크레딧
- Kazamok
- 모든 기여자들

## 📄 라이선스
이 프로젝트는 GPL-3.0 라이선스 하에 배포됩니다.
