## 📝 설명
IP Limit Manager는 AzerothCore 서버에서 동일 IP에서의 다중 접속을 제어하는 모듈입니다. 이 모듈은 서버 관리자가 특정 IP 주소에 대한 접속을 허용하거나 제한할 수 있게 해줍니다.

## ✨ 주요 기능
- 🔒 동일 IP에서의 다중 접속 제한
- 📋 허용된 IP 주소 관리 (추가/삭제/조회)
- 👮 관리자 권한으로 IP 접근 제어
- 💾 데이터베이스 기반 IP 관리

## 📋 요구사항
- AzerothCore v1.0.1+

## 🚀 설치 방법

1. 모듈을 AzerothCore 소스의 `modules` 디렉토리에 복사합니다.
2. SQL 파일을 character 데이터베이스에 임포트합니다.
3. CMake를 다시 실행하고 AzerothCore를 새로 빌드합니다.

## ⚙️ 설정 방법 (선택사항)

1. 로컬 주소 127.0.0.1 은 기본으로 추가되어 있습니다.
2. 모듈 설정을 변경하려면 configs/modules/mod-iplimit-manager.conf.dist 를 편집하세요.

## 📊 데이터
- 📌 타입: Server/Player
- 📜 스크립트: IP Limit Manager
- ⚙️ 설정: Yes
- 💾 SQL: Yes

## 👥 크레딧
- AzerothCore 팀
- 모든 기여자들

## 📄 라이선스
이 프로젝트는 GPL-3.0 라이선스 하에 배포됩니다.
