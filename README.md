# IP Limit Manager

## 📝 설명
IP Limit Manager는 AzerothCore 서버에서 동일 IP에서의 다중 접속을 제어하는 모듈입니다.
이 모듈은 서버 관리자가 특정 IP 주소에 대한 접속을 허용하거나 제한할 수 있게 해줍니다.
서버 관리자가 특정 IP 주소에 대한 접속을 허용하거나 제한할 수 있으며, 안전한 서버 운영을 위한 필수 도구입니다.

## ✨ 주요 기능
- 🔒 동일 IP에서의 다중 접속 제한
  - 기본적으로 동일 IP에서 1개의 계정만 접속 가능
  - 허용된 IP는 제한 없이 접속 가능
- 📋 허용된 IP 주소 관리 (추가/삭제/조회)
  - `.allowip add <ip>` - IP 주소를 허용 목록에 추가
  - `.allowip del <ip>` - IP 주소를 허용 목록에서 제거
- 📊 접속 로깅 시스템
  - 모든 접속 시도가 CSV 파일로 기록됨
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
2. SQL 파일을 auth 데이터베이스에 임포트합니다.
3. CMake를 다시 실행하고 AzerothCore를 새로 빌드합니다.

## ⚙️ 설정 방법 (선택사항)

1. 로컬 주소 127.0.0.1 은 기본으로 추가되어 있습니다.
2. 모듈 설정을 변경하려면 configs/modules/mod-iplimit-manager.conf.dist 를 편집하세요.
   - `EnableIpLimitManager`: 모듈 활성화 여부 (기본값: 1)
   - `IpLimitManager.Announce.Enable`: 접속 시 알림 메시지 표시 (기본값: 1)

## 📊 데이터
- 📌 타입: Server/Player
- 📜 스크립트: IP Limit Manager
- ⚙️ 로깅: CSV 파일 기반
- 💾 SQL: Yes (auth)

## 👥 크레딧
- AzerothCore 팀
- 모든 기여자들

## 📄 라이선스
이 프로젝트는 GPL-3.0 라이선스 하에 배포됩니다.
