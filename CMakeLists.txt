# Modules 폴더 내부용 CMake

set(MOD_NAME "mod-iplimit-manager")

set(MOD_SRC
    src/mod-iplimit-manager.cpp
    src/mod-iplimit-manager-loader.cpp
)

set(MOD_CONF
    conf/mod-iplimit-manager.conf.dist
)

include(${CMAKE_SOURCE_DIR}/src/cmake/macros/ConfigureModules.cmake)

configure_ac_module(${MOD_NAME})
