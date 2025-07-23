-- filename: mod-iplimit-manager.sql
-- Description: SQL script for mod-iplimit-manager (IPv4 only)

USE acore_auth;

-- 로그인 허용 IP 주소를 저장할 테이블 생성
DROP TABLE IF EXISTS `ip_login_allowlist`;
CREATE TABLE `ip_login_allowlist` (
  `ip` varchar(15) NOT NULL,
  `description` varchar(255) DEFAULT NULL,
  PRIMARY KEY (`ip`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='IP Limit Manager - 로그인 허용 IP 주소 목록';

-- 기본 localhost IP 주소 추가
INSERT INTO `ip_login_allowlist` (`ip`, `description`)
VALUES ('127.0.0.1', 'Default localhost for login');

-- 계정 생성 규칙을 저장할 테이블 생성
DROP TABLE IF EXISTS `ip_registration_rules`;
CREATE TABLE `ip_registration_rules` (
  `ip` varchar(15) NOT NULL,
  `description` varchar(255) DEFAULT NULL,
  `can_create_account` tinyint(1) NOT NULL DEFAULT '1',
  PRIMARY KEY (`ip`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='IP Limit Manager - 계정 생성 규칙';

-- 기본 localhost IP 주소 추가
INSERT INTO `ip_registration_rules` (`ip`, `description`, `can_create_account`)
VALUES ('127.0.0.1', 'Default localhost for registration', 1);

-- account_creation_log 테이블 생성 (멱등성 보장)
DROP TABLE IF EXISTS `account_creation_log`;
CREATE TABLE IF NOT EXISTS `account_creation_log` (
  `ip` varchar(15) NOT NULL,
  `creation_time` datetime NOT NULL,
  `account_id` int(10) unsigned NOT NULL,
  `account_username` varchar(50) NOT NULL,
  PRIMARY KEY (`ip`, `creation_time`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- module_configs 테이블 생성 (멱등성 보장)
DROP TABLE IF EXISTS `module_configs`;
CREATE TABLE IF NOT EXISTS `module_configs` (
  `config_name` varchar(255) NOT NULL,
  `config_value` varchar(255) NOT NULL,
  `description` varchar(255) DEFAULT NULL,
  PRIMARY KEY (`config_name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='모듈 설정 값 저장 테이블';

-- 계정 생성 IP 제한 설정 값 삽입 (멱등성 보장)
INSERT IGNORE INTO `module_configs` (`config_name`, `config_value`, `description`) VALUES
('AccountCreationIpLimit.MaxAccountsPerIp', '3', '특정 IP에서 일정 시간 내에 생성 가능한 최대 계정 수'),
('AccountCreationIpLimit.TimeframeHours', '24', '계정 생성 제한을 적용할 시간 범위(시간)');

-- 어떤 IP가 어떤 계정에 의해 사용되었는지,
-- 그리고 어떤 계정이 어떤 IP에서 사용되었는지를 추적하는 테이블을 생성합니다.
DROP TABLE IF EXISTS `account_ip_usage`;
CREATE TABLE IF NOT EXISTS `account_ip_usage` (
  `account_id` INT(10) UNSIGNED NOT NULL,
  `ip_address` VARCHAR(15) NOT NULL,
  `first_login_time` DATETIME NOT NULL,
  `last_login_time` DATETIME NOT NULL,
  `login_count` INT(10) UNSIGNED NOT NULL DEFAULT '0',
  PRIMARY KEY (`account_id`, `ip_address`),
  KEY `idx_ip_address` (`ip_address`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;