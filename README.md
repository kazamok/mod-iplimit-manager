# IP Limit & Account-IP Manager

## 📝 설명
**IP Limit & Account-IP Manager**는 AzerothCore 서버의 보안과 운영 효율을 극대화하기 위해 설계된 올인원(All-in-One) 모듈입니다. 이 모듈은 단순히 접속을 제한하는 것을 넘어, 계정과 IP의 관계를 영구적으로 기록하고 분석하여 관리자에게 강력한 통찰력을 제공합니다.

이 모듈은 **'실시간 접속 제어'**와 **'계정-IP 관계 분석'**이라는 두 가지 핵심 기능을 유기적으로 결합합니다.

- **실시간 접속 제어:** '동시 접속'과 '로그인 빈도'를 제한하여 비정상적인 접속 패턴을 실시간으로 차단합니다.
- **계정-IP 관계 분석:** 모든 성공적인 로그인을 `account_formation` 테이블에 기록하여, 어떤 계정이 어떤 IP를 사용했는지, 또는 특정 IP로 어떤 계정들이 접속했는지와 같은 연결 고리를 추적할 수 있습니다.

이를 통해 관리자는 작업장, 계정 공유, 해킹 의심 사례 등을 데이터에 기반하여 정확하게 파악하고 조치할 수 있습니다.

## ✨ 주요 기능
- **듀얼 제한 시스템:**
  - 🔒 **동시 접속 제한:** 하나의 IP에서 동시에 접속할 수 있는 최대 계정 수를 제한합니다.
  - ⏱️ **로그인 빈도 제한:** 일정 시간 내에 하나의 IP에서 로그인할 수 있는 고유 계정의 수를 제한합니다.
- **영구적인 계정-IP 로그:**
  - 📈 **관계 기록:** 모든 성공적인 로그인을 `account_formation` 테이블에 기록하여 계정과 IP의 관계를 영구적으로 저장합니다.
  - 🔎 **데이터 분석:** 최초/최종 접속 시간, 총 접속 횟수 등 풍부한 데이터를 기반으로 사용자의 접속 패턴을 분석할 수 있습니다.
- **강력한 관리자 명령어:**
  - **접속 제한 관리:** `.allowip` 명령어로 특정 IP에 대한 접속 제한 규칙(화이트리스트)을 실시간으로 관리합니다.
  - **관계 추적:** `.account ip`와 `.ip accounts` 명령어로 계정과 IP의 연결 고리를 양방향으로 추적합니다.
- **유연한 정책 관리:**
  - ⚙️ **기본 정책:** 설정 파일에서 서버 전체에 적용될 기본 규칙을 설정합니다.
  - 📋 **개별 정책 (화이트리스트):** `custom_allowed_ips` 테이블을 통해 특정 IP에만 다른 규칙을 적용합니다.
- **상세 로깅:**
  - 모든 계정의 로그인/로그아웃 활동이 `logs/iplimit/` 폴더에 CSV 파일로 기록되어 추적이 용이합니다.

## 🚀 설치 방법
1.  이 모듈 폴더를 AzerothCore 소스 트리의 `modules` 디렉토리에 복사합니다.
2.  `data/sql/db-auth/mod-iplimit-manager-integrated.sql` 파일을 `acore_auth` 데이터베이스에 임포트(import)합니다.
3.  CMake를 다시 실행하고 AzerothCore를 새로 빌드합니다.

## ⚙️ 설정 및 사용법 (`mod-iplimit-manager.conf`)

### 1. 접속 제한 설정
- `EnableIpLimitManager`: 접속 제한 기능을 활성화합니다. (기본값: 1)
- `IpLimitManager.Announce.Enable`: 접속 시 제한 정책을 알리는 메시지를 표시합니다. (기본값: 1)
- `IpLimitManager.Max.Account.Enable`: 동시 접속 제한 기능을 켜거나 끕니다. (기본값: 1)
- `IpLimitManager.Max.Account`: 허용할 **최대 동시 접속** 계정 수를 설정합니다. (기본값: 1)
- `IpLimitManager.RateLimit.Enable`: 로그인 빈도 제한 기능을 켜거나 끕니다. (기본값: 1)
- `IpLimitManager.RateLimit.TimeWindowSeconds`: 고유 계정 수를 체크할 시간 범위(초)를 설정합니다. (기본값: 3600)
- `IpLimitManager.RateLimit.MaxUniqueAccounts`: 위 시간 동안 허용할 **최대 고유 계정** 수를 설정합니다. (기본값: 1)

### 2. 계정-IP 로거 설정
- `AccountIpLogger.Enable`: 계정-IP 관계 기록 기능을 활성화합니다. (기본값: 1)
- `AccountIpLogger.Log.GM.Enable`: GM 계정의 접속 기록을 남길지 여부를 설정합니다. (기본값: 0)

## 🛠️ 인게임 명령어

### 화이트리스트 관리 (`.allowip`)
- `.allowip append <ip> [max_conn] [max_unique]`
  - 화이트리스트에 IP를 추가하고 개별 규칙을 설정합니다.
- `.allowip remove <ip>`
  - 화이트리스트에서 IP를 제거합니다.
- `.allowip show`
  - 화이트리스트에 등록된 모든 IP와 설정을 보여줍니다.

### 계정-IP 관계 분석
- `.account ip <캐릭터이름>`
  - 특정 계정이 사용했던 모든 IP 주소 목록과 상세 정보를 보여줍니다.
- `.ip accounts <IP주소>`
  - 특정 IP 주소로 접속했던 모든 계정 목록을 보여줍니다.

## 👥 크레딧
- Kazamok
- Gemini
- 모든 기여자들

## 📄 라이선스
이 프로젝트는 GPL-3.0 라이선스 하에 배포됩니다.