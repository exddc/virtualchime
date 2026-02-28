cmake_minimum_required(VERSION 3.28)
project(chime LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MOSQ QUIET libmosquitto)

if(MOSQ_FOUND)
  set(VC_MQTT_CLIENT_SOURCE ../common/src/mqtt/client.cpp)
else()
  message(WARNING "libmosquitto not found; building with MQTT stub client")
  set(VC_MQTT_CLIENT_SOURCE ../common/src/mqtt/client_stub.cpp)
endif()
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

add_library(
  vc_common STATIC
  ../common/src/logging/logger.cpp
  ${VC_MQTT_CLIENT_SOURCE}
  ../common/src/runtime/signal_handler.cpp
  ../common/src/util/environment.cpp
  ../common/src/util/filesystem.cpp
  ../common/src/util/platform.cpp
  ../common/src/util/strings.cpp
  ../common/src/util/time.cpp)
target_include_directories(vc_common PUBLIC ../common/include)
target_compile_options(vc_common PRIVATE -Wall -Wextra -Wpedantic)
if(MOSQ_FOUND)
  target_include_directories(vc_common PRIVATE ${MOSQ_INCLUDE_DIRS})
  target_compile_options(vc_common PRIVATE ${MOSQ_CFLAGS_OTHER})
  target_link_libraries(vc_common PRIVATE ${MOSQ_LIBRARIES})
endif()

add_library(
  chime_core STATIC
  src/audio/aplay_audio_player.cpp
  src/config/chime_config.cpp
  src/network/linux_wifi_monitor.cpp
  src/service/chime_service.cpp)
target_include_directories(chime_core PUBLIC include ../common/include)
target_compile_options(chime_core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(chime_core PUBLIC vc_common)

add_library(
  chime_webd_core STATIC
  src/webd/apply_manager.cpp
  src/webd/config_store.cpp
  src/webd/json.cpp
  src/webd/mdns.cpp
  src/webd/string_utils.cpp
  src/webd/ui_assets.cpp
  src/webd/web_server.cpp
  src/webd/wifi_scan.cpp)
target_include_directories(chime_webd_core PUBLIC include ../common/include)
target_compile_options(chime_webd_core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(
  chime_webd_core
  PUBLIC vc_common OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

add_executable(chime src/main.cpp)
target_include_directories(chime PRIVATE include ../common/include)
target_compile_options(chime PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(chime PRIVATE chime_core vc_common)

add_executable(chime-webd src/webd/main.cpp)
target_include_directories(chime-webd PRIVATE include ../common/include)
target_compile_options(chime-webd PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(
  chime-webd
  PRIVATE chime_webd_core vc_common OpenSSL::SSL OpenSSL::Crypto
          Threads::Threads)
