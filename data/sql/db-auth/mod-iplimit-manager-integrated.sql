-- ================================================================= --
--      SQL Script for `mod-iplimit-manager` (Integrated)        --
-- ================================================================= --
-- This single file creates all necessary tables for the module.
--

--
-- Table structure for table `custom_allowed_ips`
-- 설명: 사용자 정의 연결 제한이 있는 허용 목록에 있는 IP를 저장합니다.
--
DROP TABLE IF EXISTS `custom_allowed_ips`;
CREATE TABLE `custom_allowed_ips` (
  `ip` varchar(15) NOT NULL DEFAULT '127.0.0.1' COMMENT 'IPv4 Address',
  `description` varchar(255) DEFAULT NULL COMMENT 'IP 주소에 대한 설명',
  `max_connections` int unsigned NOT NULL DEFAULT 2 COMMENT '이 IP에 허용되는 최대 연결 수',
  `max_unique_accounts` int unsigned NOT NULL DEFAULT 1 COMMENT '시간 빈도 우회에 허용되는 최대 고유 계정 수',
  PRIMARY KEY (`ip`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='IP Limit Manager - 허용된 IP 주소';

--
-- Insert default data for `custom_allowed_ips`
-- 개발PC용
--
INSERT INTO `custom_allowed_ips` (`ip`, `description`, `max_connections`, `max_unique_accounts`) 
VALUES ('127.0.0.1', 'Default localhost IP - System', 2, 2);


--
-- Table structure for table `account_formation`
-- 설명: 계정과 IP 주소 간의 관계와 기록을 추적합니다.
--
DROP TABLE IF EXISTS `account_formation`;
CREATE TABLE `account_formation` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '고유 식별자',
  `accountId` INT UNSIGNED NOT NULL COMMENT '계정 ID (from acore_auth.account.id)',
  `ipAddress` VARCHAR(45) NOT NULL COMMENT '로그인 IP 주소 (IPv4/IPv6 compatible)',
  `firstSeen` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '이 IP에서 첫 번째 로그인된 타임스탬프',
  `lastSeen` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '이 IP에서 가장 최근에 로그인한 타임스탬프',
  `loginCount` INT UNSIGNED NOT NULL DEFAULT 1 COMMENT '이 IP에서 로그인한 총 수',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_account_ip` (`accountId`, `ipAddress`),
  KEY `idx_ipAddress` (`ipAddress`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='IP Limit Manager - Logger, 계정 및 IP 관계를 추적합니다.';

