-- Create a table to store allowed IP addresses
CREATE TABLE IF NOT EXISTS `custom_allowed_ips` (
  `ip` VARCHAR(45) NOT NULL,
  PRIMARY KEY (`ip`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
