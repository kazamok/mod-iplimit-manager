# IP Limit Manager

## 📝 소개
IP Limit Manager는 AzerothCore 서버에서 동일 IP에서의 다중 접속을 제어하는 모듈입니다. 
서버 관리자가 특정 IP 주소에 대한 접속을 허용하거나 제한할 수 있으며, 안전한 서버 운영을 위한 필수 도구입니다.

## ✨ 주요 기능
- 🔒 동일 IP에서의 다중 접속 제한
  - 기본적으로 동일 IP에서 1개의 계정만 접속 가능
  - 허용된 IP는 제한 없이 접속 가능
- 📋 허용된 IP 주소 관리
  - `.allowip add <ip>` - IP 주소를 허용 목록에 추가
  - `.allowip del <ip>` - IP 주소를 허용 목록에서 제거
- 📊 접속 로깅 시스템
  - 모든 접속 시도가 CSV 파일로 기록됨
  - 로그 파일명: `access_log_YYYY-MM-DD_HHMMSS.csv`
- 🛡️ 보안 기능
  - 비허용 IP의 다중 접속 시도 시 자동 차단
  - 서버 재시작 시에도 설정 유지

## 📋 시스템 요구사항
- AzerothCore v1.0.1 이상
- MySQL 5.7 이상
- ACE ≥ 7.0.0
- C++ 17 지원 컴파일러

## 🚀 설치 방법
1. 모듈 설치
```bash
git clone https://github.com/your-repo/mod-iplimit-manager.git
cd azerothcore-wotlk/modules/
cp -r mod-iplimit-manager .
```

2. 빌드 및 설치
```bash
cd azerothcore-wotlk
cmake . -DMODULE=1 -DTOOLS=0
make -j$(nproc)
make install
```

## ⚙️ 설정 방법
1. 기본 설정
   - 로컬호스트(127.0.0.1)는 기본적으로 허용 목록에 포함
   - 설정 파일: `mod-iplimit-manager.conf.dist`

2. 주요 설정 옵션
   - `EnableIpLimitManager`: 모듈 활성화 여부 (기본값: 1)
   - `IpLimitManager.Announce.Enable`: 접속 시 알림 메시지 표시 (기본값: 0)

## 📊 기술 스펙
- 타입: Server/Player Script
- 데이터베이스: MySQL (auth)
- 로깅: CSV 파일 기반
- 권한: SEC_ADMINISTRATOR

## 👥 크레딧
- 개발: AzerothCore 커뮤니티
- 기여자: [기여자 목록]

## 📄 라이선스
이 프로젝트는 AGPL 3.0 라이선스 하에 배포됩니다.
자세한 내용은 [LICENSE](LICENSE) 파일을 참조하세요.
