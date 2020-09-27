cmake_minimum_required(VERSION 3.17)
project(multiplex C)

set(CMAKE_C_STANDARD 11)

add_executable(multiplex1 select/server.c)
add_executable(multiplex2 select/client.c)