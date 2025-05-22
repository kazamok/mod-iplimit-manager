@ -1,17 +1,17 @@
# mod-iplimit-manager
The AzerothCore module `mod-iplimit-manager` limits connections to only one client from the same IP address.
이 AzerothCore 모듈 `mod-iplimit-manager`는 동일한 IP 주소에서 하나의 클라이언트 연결만 허용합니다.

## 기능
- 기본적으로 한 IP당 하나의 연결만 허용됩니다.
- 명령어를 통해 예외 IP를 추가하거나 제거할 수 있습니다.
- 설정 파일에서 모듈을 활성화하고 메시지를 설정할 수 있습니다.

## 명령어
- `.allowip add <ip>` : 허용된 IP 목록에 추가
- `.allowip del <ip>` : 허용된 IP 목록에서 제거

## 설치 가이드
1. 모듈을 `modules/` 폴더에 복사합니다.
2. `mod-iplimit-manager.conf.dist`를 `mod-iplimit-manager.conf`로 복사하고 수정합니다.
3. `mod-iplimit-manager.sql`을 월드 DB에 적용합니다.
4. CMake를 재생성하고 빌드합니다.
