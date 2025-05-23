-- filename: mod-iplimit-manager.sql
-- Description: SQL script for mod-iplimit-manager (IPv4 only)

CREATE DATABASE IF NOT EXISTS `acore_world`;
USE `acore_world`;

DROP TABLE IF EXISTS `custom_allowed_ips`;

CREATE TABLE `custom_allowed_ips` (
  `ip` varchar(15) NOT NULL DEFAULT '127.0.0.1',
  PRIMARY KEY (`ip`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='List of allowed IPv4 addresses';

-- Note: description column intentionally omitted as IPv6/annotations are not required.
