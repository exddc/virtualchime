cmake_minimum_required(VERSION 4.0)
project(chime LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MOSQ REQUIRED libmosquitto)

add_executable(chime src/main.cpp)
target_include_directories(chime PRIVATE ${MOSQ_INCLUDE_DIRS})
target_link_libraries(chime PRIVATE ${MOSQ_LIBRARIES})
target_compile_options(chime PRIVATE ${MOSQ_CFLAGS_OTHER})
