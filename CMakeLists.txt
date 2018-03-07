cmake_minimum_required(VERSION 3.1)
project(duze4)

set(CMAKE_C_FLAGS "-g -Wall")

add_executable(Sort main.c)
target_link_libraries(Sort rt pthread)

install(TARGETS DESTINATION .)
