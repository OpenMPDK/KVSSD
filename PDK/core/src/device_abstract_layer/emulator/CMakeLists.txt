cmake_minimum_required(VERSION 2.8.9)

# output detailed command
set( CMAKE_VERBOSE_MAKEFILE on )
set(ALLOW_DUPLICATE_CUSTOM_TARGETS TRUE)

project (libkvemul C CXX)

# build type dependent flags for Linux
set(CXX_FLAGS_DEBUG_OPTIONS "-g -DENABLE_LOGGING")
set(CXX_FLAGS_RELEASE_OPTIONS  "-g -O2")

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${CXX_FLAGS_DEBUG_OPTIONS}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${CXX_FLAGS_RELEASE_OPTIONS}")

set(LINK_LIBS -pthread -Wl,--no-as-needed -fPIC -lrt -lkvemul -L. -march=native)
set(LINK_LIBS_STATIC -pthread -Wl,--no-as-needed -fPIC -lrt -lkvemul_static -L. -march=native)

## add "-O2" back for release
set(kvemul_CFLAGS "-MMD -MP -Wall -DLINUX -D_FILE_OFFSET_BITS=64 -fPIC  -march=native")
set(kvemul_CXXFLAGS "-O2 -g -std=c++11 -MMD -MP -Wall -DLINUX -D_FILE_OFFSET_BITS=64 -fPIC  -march=native")
set(kvemul_CFLAGS "-MMD -MP -Wall -DLINUX -D_FILE_OFFSET_BITS=64 -fPIC  -march=native")
 
include_directories (../../../../core/include)
include_directories (../include)
include_directories (../include/private)

# SOURCE CODE
SET(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io_cmd.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/kv_config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/kv_device.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/kv_namespace.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/kv_emulator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/kvs_adi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/thread_pool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/queue.cpp
)

SET(HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/kvs_adi.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/private/kvs_adi_internal.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/private/io_cmd.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/private/history.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/private/kv_config.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/private/kv_device.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/private/kv_namespace.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/private/kv_emulator.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/private/kvs_utils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/private/queue.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/private/thread_pool.hpp
)

SET(APP_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/kvs_adi.h
)

# add_definitions(-DWITH_SPDK)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${kvemul_CFLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${kvemul_CXXFLAGS}")

#
# library build rules (shared)
#
add_library(kvemul_static STATIC ${SOURCES} ${HEADERS})
add_library(kvemul SHARED ${SOURCES} ${HEADERS})

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/kvssd_emul.conf ${CMAKE_CURRENT_BINARY_DIR}/kvssd_emul.conf COPYONLY)

#
# test
#
add_executable(sample_poll_emul ${CMAKE_CURRENT_SOURCE_DIR}/sample_code/sample_poll.cpp ${HEADERS})
target_link_libraries(sample_poll_emul ${LINK_LIBS})
add_dependencies(sample_poll_emul kvemul)

add_executable(sample_interrupt_emul ${CMAKE_CURRENT_SOURCE_DIR}/sample_code/sample_interrupt.cpp ${HEADERS})
target_link_libraries(sample_interrupt_emul ${LINK_LIBS})
add_dependencies(sample_interrupt_emul kvemul)

add_executable(test_suite_emul ${CMAKE_CURRENT_SOURCE_DIR}/test/test_suite.cpp ${HEADERS})
target_link_libraries(test_suite_emul ${LINK_LIBS})
add_dependencies(test_suite_emul kvemul)

ADD_CUSTOM_TARGET(release_emul
  COMMAND ${CMAKE_COMMAND} -DCMAKE_BUILD_TYPE=Release .
  COMMAND ${CMAKE_COMMAND} --build . --target clean
  COMMAND make -j40
  COMMAND ${CMAKE_COMMAND} --build . --target all
  #COMMAND ${CMAKE_COMMAND} --build . --target install
  COMMENT "Switch CMAKE_BUILD_TYPE to Release & Install"
)

ADD_CUSTOM_TARGET(debug_emul
  COMMAND ${CMAKE_COMMAND} -DCMAKE_BUILD_TYPE=Debug .
  COMMAND make VERBOSE=1 -j40
  #COMMAND ${CMAKE_COMMAND} --build . --target install
  #COMMAND ${CMAKE_COMMAND} --build . --target all
  COMMENT "Switch CMAKE_BUILD_TYPE to Debug"
)
