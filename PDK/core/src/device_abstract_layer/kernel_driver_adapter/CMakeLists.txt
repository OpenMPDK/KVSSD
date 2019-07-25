cmake_minimum_required(VERSION 2.8.9)

# output detailed command
set( CMAKE_VERBOSE_MAKEFILE on )
set(ALLOW_DUPLICATE_CUSTOM_TARGETS TRUE)

project (libkvkdd C CXX)

# build type dependent flags for Linux
set(CXX_FLAGS_DEBUG_OPTIONS "-g -DENABLE_LOGGING")
set(CXX_FLAGS_RELEASE_OPTIONS  "-g -O2")

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${CXX_FLAGS_DEBUG_OPTIONS}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${CXX_FLAGS_RELEASE_OPTIONS}")

set(LINK_LIBS -pthread -Wl,--no-as-needed -fPIC -lrt -lkvkdd -L. -march=native)
set(LINK_LIBS_STATIC -pthread -Wl,--no-as-needed -fPIC -lrt -lkvkdd_static -L. -march=native)

## add "-O2" back for release
set(kvkdd_CFLAGS "-MMD -MP -Wall -DLINUX -D_FILE_OFFSET_BITS=64 -fPIC  -march=native")
set(kvkdd_CXXFLAGS "-O2 -g -std=c++11 -MMD -MP -Wall -DLINUX -D_FILE_OFFSET_BITS=64 -fPIC  -march=native")
set(kvkdd_CFLAGS "-MMD -MP -Wall -DLINUX -D_FILE_OFFSET_BITS=64 -fPIC  -march=native")
 
include_directories (../../../../core/include)
include_directories (../include)
include_directories (../include/private)
include_directories (../kernel_driver_adapter)

# SOURCE CODE
SET(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/kadi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/kadi_debug.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/kvs_adi.cpp
)

SET(HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/private/kvs_adi_internal.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/private/io_cmd.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/private/kv_device.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/private/kv_namespace.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/private/kvs_utils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/private/queue.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/linux_nvme_ioctl.h
    ${CMAKE_CURRENT_SOURCE_DIR}/kadi.h
    ${CMAKE_CURRENT_SOURCE_DIR}/kadi_debug.h
)

SET(APP_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/kvs_adi.h
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${kvkdd_CFLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${kvkdd_CXXFLAGS}")

#
# library build rules (shared)
#
add_library(kvkdd_static STATIC ${SOURCES} ${HEADERS})
add_library(kvkdd SHARED ${SOURCES} ${HEADERS})

#
# test
#
add_executable(sample_poll ${CMAKE_CURRENT_SOURCE_DIR}/sample_code/sample_poll.cpp ${HEADERS})
target_link_libraries(sample_poll ${LINK_LIBS})
add_dependencies(sample_poll kvkdd)

add_executable(sample_interrupt ${CMAKE_CURRENT_SOURCE_DIR}/sample_code/sample_interrupt.cpp ${HEADERS})
target_link_libraries(sample_interrupt ${LINK_LIBS})
add_dependencies(sample_interrupt kvkdd)

add_executable(test_suite ${CMAKE_CURRENT_SOURCE_DIR}/test/test_suite.cpp ${HEADERS})
target_link_libraries(test_suite ${LINK_LIBS})
add_dependencies(test_suite kvkdd)

ADD_CUSTOM_TARGET(release
  COMMAND ${CMAKE_COMMAND} -DCMAKE_BUILD_TYPE=Release .
  COMMAND ${CMAKE_COMMAND} --build . --target clean
  COMMAND make -j40
  COMMAND ${CMAKE_COMMAND} --build . --target all
  #COMMAND ${CMAKE_COMMAND} --build . --target install
  COMMENT "Switch CMAKE_BUILD_TYPE to Release & Install"
)

ADD_CUSTOM_TARGET(debug
  COMMAND ${CMAKE_COMMAND} -DCMAKE_BUILD_TYPE=Debug .
  COMMAND make VERBOSE=1 -j40
  COMMENT "Switch CMAKE_BUILD_TYPE to Debug"
)
