-- C:\azerothcore\modules\mod-iplimit-manager\data\sql\db-auth\account_ip_usage.sql
-- 이 파일은 어떤 IP가 어떤 계정에 의해 사용되었는지,
-- 그리고 어떤 계정이 어떤 IP에서 사용되었는지를 추적하는 테이블을 생성합니다.

USE acore_auth;

CREATE TABLE IF NOT EXISTS `account_ip_usage` (
  `account_id` INT(10) UNSIGNED NOT NULL,
  `ip_address` VARCHAR(15) NOT NULL,
  `first_login_time` DATETIME NOT NULL,
  `last_login_time` DATETIME NOT NULL,
  `login_count` INT(10) UNSIGNED NOT NULL DEFAULT '0',
  PRIMARY KEY (`account_id`, `ip_address`),
  KEY `idx_ip_address` (`ip_address`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;