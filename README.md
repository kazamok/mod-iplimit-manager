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

1. 로컬 주소 127.0.0.1 은 기본으로 추가되어 있습니다.
2. 모듈 설정을 변경하려면 `configs/modules/mod-iplimit-manager.conf.dist` 및 `authserver.conf.dist` 파일을 편집하세요.
   - `EnableIpLimitManager`: 모듈 활성화 여부 (기본값: 1)
   - `IpLimitManager.Announce.Enable`: 접속 시 알림 메시지 표시 (기본값: 1)
   - `IpLimitManager.MaxConnectionsPerIp`: 특정 IP에서 허용할 최대 동시 접속 계정 수 (기본값: 1)
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
  - `module_configs` 테이블: 모듈 설정 값 중앙 관리

## 👥 크레딧
- Kazamok
- 모든 기여자들

## 📄 라이선스
이 프로젝트는 GPL-3.0 라이선스 하에 배포됩니다.