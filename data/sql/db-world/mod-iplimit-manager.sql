-- 허용된 IP 주소를 저장하는 테이블 생성
CREATE TABLE IF NOT EXISTS `custom_allowed_ips` (
  `ip` VARCHAR(45) NOT NULL,
  PRIMARY KEY (`ip`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
