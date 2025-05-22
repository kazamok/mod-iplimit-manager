# [logo](https://raw.githubusercontent.com/azerothcore/azerothcore.github.io/master/images/logo-github.png) AzerothCore

## IPLIMIT MANAGER

- 최신 빌드 상태: [![Build Status]](https://github.com/kazamok/mod-iplimit-manager)

## 중요 사항

- 최소한 AzerothCore 커밋 9adba48을 사용해야 합니다.

## 기능

- 동일한 IP 주소에서 하나의 클라이언트 연결만 허용합니다.

- 명령어를 통해 예외 IP를 추가하거나 제거할 수 있습니다.

- 설정 파일에서 모듈을 활성화하고 메시지를 설정할 수 있습니다.

## 명령어
- `.allowip add <ip>` : 허용된 IP 목록에 추가

- `.allowip del <ip>` : 허용된 IP 목록에서 제거

## 설치 가이드
- 모듈을 `modules` 폴더에 복사합니다.

- `mod-iplimit-manager.conf.dist`를 `mod-iplimit-manager.conf`로 복사하고 수정합니다.

- `mod-iplimit-manager.sql`을 월드 DB에 적용합니다.

- CMake를 재생성하고 빌드합니다.
