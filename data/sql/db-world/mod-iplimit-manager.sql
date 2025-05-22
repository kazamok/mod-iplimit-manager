-- Create a table to store allowed IP addresses

CREATE TABLE IF NOT EXISTS `acore_world.custom_allowed_ips` (
  `ip` varchar(45) NOT NULL,
  `text` text CHARACTER SET utf8mb3 COLLATE utf8mb3_general_ci,
  PRIMARY KEY (`ip`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
