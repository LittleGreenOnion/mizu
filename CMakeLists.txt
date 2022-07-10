cmake_minimum_required(VERSION 3.10)
project(main)
set(CMAKE_CXX_STANDARD 14)

set(CMAKE_BINARY_DIR ${CMAKE_SOURCE_DIR}/bin)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})

add_compile_options(-O2 -Wall -std=c++14 -funsigned-char ${Static_STD_LIBRARIES})

include_directories(src 3rdParty)
set(SOURCE_FILES 
  src/main.cpp
  src/order.cpp
  src/utils.cpp
  src/exchange_engine.cpp)

add_executable(main ${SOURCE_FILES})
target_link_libraries(main pthread)
