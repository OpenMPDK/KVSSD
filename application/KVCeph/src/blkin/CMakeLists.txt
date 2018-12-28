cmake_minimum_required(VERSION 2.8.11)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/modules/")  
find_package(LTTng REQUIRED)

# make && make test
enable_testing()

add_subdirectory(blkin-lib)
