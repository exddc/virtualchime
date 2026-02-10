cmake_minimum_required(VERSION 4.0)
project(chime LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MOSQ REQUIRED libmosquitto)

add_library(
  vc_common STATIC
  ../common/src/logging/logger.cpp
  ../common/src/mqtt/client.cpp
  ../common/src/runtime/signal_handler.cpp
  ../common/src/util/environment.cpp
  ../common/src/util/filesystem.cpp
  ../common/src/util/platform.cpp
  ../common/src/util/strings.cpp
  ../common/src/util/time.cpp)
target_include_directories(vc_common PUBLIC ../common/include)
target_include_directories(vc_common PRIVATE ${MOSQ_INCLUDE_DIRS})
target_compile_options(vc_common PRIVATE -Wall -Wextra -Wpedantic ${MOSQ_CFLAGS_OTHER})
target_link_libraries(vc_common PRIVATE ${MOSQ_LIBRARIES})

add_library(
  chime_core STATIC
  src/audio/aplay_audio_player.cpp
  src/config/chime_config.cpp
  src/network/linux_wifi_monitor.cpp
  src/service/chime_service.cpp)
target_include_directories(chime_core PUBLIC include ../common/include)
target_compile_options(chime_core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(chime_core PUBLIC vc_common)

add_executable(chime src/main.cpp)
target_include_directories(chime PRIVATE include ../common/include)
target_compile_options(chime PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(chime PRIVATE chime_core vc_common)
