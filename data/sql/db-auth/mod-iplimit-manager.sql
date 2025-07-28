-- filename: mod-iplimit-manager.sql
-- Description: SQL script for mod-iplimit-manager (IPv4 only)

-- 기존 테이블이 있다면 삭제
DROP TABLE IF EXISTS `custom_allowed_ips`;

-- 허용된 IP 주소를 저장할 테이블 생성
CREATE TABLE `custom_allowed_ips` (
  `ip` varchar(15) NOT NULL DEFAULT '127.0.0.1' COMMENT 'IPv4 주소',
  `description` varchar(255) DEFAULT NULL COMMENT 'IP 주소에 대한 설명',
  `max_connections` int unsigned NOT NULL DEFAULT 2 COMMENT '이 IP에 허용된 최대 연결 수',
  `max_unique_accounts` int unsigned NOT NULL DEFAULT 1 COMMENT '시간 내 허용되는 최대 고유 계정 수',
  PRIMARY KEY (`ip`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='IP Limit Manager - 허용된 IP 주소 목록';

-- 기본 localhost IP 주소 추가
INSERT INTO `custom_allowed_ips` (`ip`, `description`, `max_connections`, `max_unique_accounts`) 
VALUES ('127.0.0.1', '기본 localhost IP - 시스템', 2, 1);

-- 권한 설정 (필요한 경우 사용자 이름과 호스트를 적절히 수정하세요)
-- GRANT SELECT, INSERT, UPDATE, DELETE ON acore_auth.custom_allowed_ips TO 'acore'@'localhost';
-- FLUSH PRIVILEGES;

-- Note: description column intentionally omitted as IPv6/annotations are not required. 

-- 참고: 이 스크립트는 IPv4 주소만 지원합니다.
-- IPv6 주소는 지원하지 않습니다. 
