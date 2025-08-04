-- ================================================================= --
--      SQL Script for `mod-iplimit-manager` (Integrated)        --
-- ================================================================= --
-- This single file creates all necessary tables for the module.
--

--
-- Table structure for table `custom_allowed_ips`
-- Description: Stores whitelisted IPs with custom connection limits.
--
DROP TABLE IF EXISTS `custom_allowed_ips`;
CREATE TABLE `custom_allowed_ips` (
  `ip` varchar(15) NOT NULL DEFAULT '127.0.0.1' COMMENT 'IPv4 Address',
  `description` varchar(255) DEFAULT NULL COMMENT 'Description for the IP address',
  `max_connections` int unsigned NOT NULL DEFAULT 2 COMMENT 'Max allowed connections for this IP',
  `max_unique_accounts` int unsigned NOT NULL DEFAULT 1 COMMENT 'Max unique accounts allowed in the time window',
  PRIMARY KEY (`ip`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='IP Limit Manager - Whitelisted IP Addresses';

--
-- Insert default data for `custom_allowed_ips`
--
INSERT INTO `custom_allowed_ips` (`ip`, `description`, `max_connections`, `max_unique_accounts`) 
VALUES ('127.0.0.1', 'Default localhost IP - System', 2, 2);


--
-- Table structure for table `account_formation`
-- Description: Tracks the relationship and history between accounts and IP addresses.
--
DROP TABLE IF EXISTS `account_formation`;
CREATE TABLE `account_formation` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT 'Unique Identifier',
  `accountId` INT UNSIGNED NOT NULL COMMENT 'Account ID (from acore_auth.account.id)',
  `ipAddress` VARCHAR(45) NOT NULL COMMENT 'Login IP Address (IPv4/IPv6 compatible)',
  `firstSeen` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT 'Timestamp of the first login from this IP',
  `lastSeen` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT 'Timestamp of the most recent login from this IP',
  `loginCount` INT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'Total number of logins from this IP',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_account_ip` (`accountId`, `ipAddress`),
  KEY `idx_ipAddress` (`ipAddress`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Account-IP-Logger - Tracks account and IP relationships.';
