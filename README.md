# mod-iplimit-manager

AzerothCore 모듈 `mod-iplimit-manager`는 동일한 IP 주소에서 오직 하나의 클라이언트만 접속할 수 있도록 제한합니다.

## 기능
- 기본적으로 하나의 IP당 하나의 연결만 허용합니다.
- 명령어로 예외 IP를 허용하거나 제거할 수 있습니다.
- 구성 파일에서 모듈 활성화 및 메시지 설정이 가능합니다.

## 명령어
- `.allowip add <ip>` : 허용 IP 목록에 추가
- `.allowip del <ip>` : 허용 IP 목록에서 제거

## 설치 방법
1. 모듈을 `modules/` 폴더에 복사합니다.
2. `mod-iplimit-manager.conf.dist`를 `mod-iplimit-manager.conf`로 복사하고 수정합니다.
3. `mod-iplimit-manager.sql`을 world DB에 적용합니다.
4. CMake 재생성 후 빌드합니다.
