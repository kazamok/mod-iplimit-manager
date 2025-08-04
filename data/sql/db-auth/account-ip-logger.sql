--
-- Table structure for table `account_formation`
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
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Tracks the relationship and history between accounts and IP addresses.';
