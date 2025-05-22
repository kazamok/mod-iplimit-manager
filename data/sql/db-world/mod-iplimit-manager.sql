-- 허용된 IP 주소를 저장하는 테이블 생성

CREATE TABLE IF NOT EXISTS `acore_world.custom_allowed_ips` (
  `ip` varchar(45) NOT NULL,
  `text` text CHARACTER SET utf8mb3 COLLATE utf8mb3_general_ci,
  PRIMARY KEY (`ip`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
