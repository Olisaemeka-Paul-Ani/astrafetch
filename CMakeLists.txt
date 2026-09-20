cmake_minimum_required(VERSION 3.21)
project(astrafetch CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(cpr CONFIG REQUIRED)

add_executable(astrafetch src/main.cpp)

target_link_libraries(astrafetch PRIVATE cpr::cpr)
