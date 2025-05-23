-- filename: mod-iplimit-manager.sql
-- Description: SQL script for mod-iplimit-manager (IPv4 only)

CREATE DATABASE IF NOT EXISTS `acore_auth`;
USE `acore_auth`;

DROP TABLE IF EXISTS `custom_allowed_ips`;

CREATE TABLE `custom_allowed_ips` (
  `ip` varchar(15) NOT NULL DEFAULT '127.0.0.1',
  `description` varchar(255) DEFAULT NULL,
  PRIMARY KEY (`ip`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='List of allowed IPs';

-- 기본 로컬호스트 IP 추가
INSERT INTO `custom_allowed_ips` (`ip`, `description`) 
VALUES ('127.0.0.1', 'Default localhost');

-- Note: description column intentionally omitted as IPv6/annotations are not required. 
