cmake_minimum_required(VERSION 3.5)

project(mod-iplimit-manager)

# 모듈 소스 파일 설정
set(SOURCES
    src/mod-iplimit-manager.cpp
    src/mod-iplimit-manager-loader.cpp
)

# 모듈 헤더 파일 설정
set(HEADERS
    src/mod-iplimit-manager.h
)

# 모듈 라이브러리 생성
add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

# AzerothCore 헤더 경로 추가
target_include_directories(${PROJECT_NAME})

# AzerothCore 라이브러리 링크
target_link_libraries(${PROJECT_NAME} PRIVATE acore) 
