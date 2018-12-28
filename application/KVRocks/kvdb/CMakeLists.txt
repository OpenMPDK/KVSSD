cmake_minimum_required(VERSION 2.8.9)
project (kvdb C CXX)


# output detailed command
set( CMAKE_VERBOSE_MAKEFILE on )
set( INSDBDIR ${CMAKE_CURRENT_SOURCE_DIR}/../insdb)

set (KVDB_VERSION_MAJOR 0)
set (KVDB_VERSION_MINOR 3)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -W -Wextra -Wall")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wsign-compare -Wshadow -Wno-unused-parameter -Wno-unused-variable -Woverloaded-virtual -Wnon-virtual-dtor -Wno-missing-field-initializers -Wno-strict-aliasing")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -march=native")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-builtin-memcmp")
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -fno-omit-frame-pointer -DNDEBUG")
  include(CheckCXXCompilerFlag)
  CHECK_CXX_COMPILER_FLAG("-momit-leaf-frame-pointer" HAVE_OMIT_LEAF_FRAME_POINTER)
  if(HAVE_OMIT_LEAF_FRAME_POINTER)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -momit-leaf-frame-pointer")
  endif()
endif()

#set(CMAKE_CXX_STANDARD 14)
#set(CMAKE_CXX_STANDARD_REQUIRED on)
include(CheckCXXSourceCompiles)
CHECK_CXX_SOURCE_COMPILES("
#include <cstdint>
#include <nmmintrin.h>
int main() {
  volatile uint32_t x = _mm_crc32_u32(0, 0);
}
" HAVE_SSE42)
if(HAVE_SSE42)
  add_definitions(-DHAVE_SSE42)
elseif(FORCE_SSE42)
  message(FATAL_ERROR "FORCE_SSE42=ON but unable to compile with SSE4.2 enabled")
endif()

CHECK_CXX_SOURCE_COMPILES("
#if defined(_MSC_VER) && !defined(__thread)
#define __thread __declspec(thread)
#endif
int main() {
  static __thread int tls;
}
" HAVE_THREAD_LOCAL)
if(HAVE_THREAD_LOCAL)
  add_definitions(-DROCKSDB_SUPPORT_THREAD_LOCAL)
endif()

option(FAIL_ON_WARNINGS "Treat compile warnings as errors" ON)
if(FAIL_ON_WARNINGS)
  if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /WX")
  else() # assume GCC
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")
  endif()
endif()


if(WITH_ASAN)
set(HAVE_ASAN)
set(LINK_ASAN "-lasan")
add_compile_options(-fsanitize=address)
else()

# find JEMalloc first
find_library(JEMALLOC_LIB jemalloc)
if(NOT "$JEMALLOC_LIB" STREQUAL "")
  set(HAVE_JEMALLOC 1)
  set(LINK_THIRDPARTY_MALLOC "-ljemalloc")
  add_compile_options(-fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free)
  add_definitions("-DROCKSDB_JEMALLOC -DJEMALLOC_NO_DEMANGLE")
  message( "jemalloc found: ${JEMALLOC_LIB}")
else()
# find TCMalloc
find_library(TCMALLOC_LIB tcmalloc)
if(NOT "$TCMALLOC_LIB" STREQUAL "")
  set(HAVE_TCMALLOC 1)
  set(LINK_THIRDPARTY_MALLOC "-ltcmalloc")
  add_compile_options(-fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free)
  message( "tcmalloc found: ${TCMALLOC_LIB}")
endif()

endif()

endif()

if(EXISTS "${PROJECT_SOURCE_DIR}/gflags-v2.2.1/build/lib/libgflags.a")
  set(HAVE_GFLAGS 1)
  add_compile_options(-DGFLAGS=google)
else()
  message( FATAL_ERROR "Please build ${PROJECT_SOURCE_DIR}/gflags-v2.2.1/build/lib/libgflags.a")
endif()

find_library(FOLLY_LIB folly)
if(NOT "$FOLLY_LIB" STREQUAL "")
  set(HAVE_FOLLY 1)
  set(LINK_FOLLY "-l:libfolly.a -ldl -lglog -ldouble-conversion")
endif()

#  header file to pass some of the CMake settings
# to the source code
configure_file (
  "${PROJECT_SOURCE_DIR}/kvdb_config.h.in"
  "${PROJECT_SOURCE_DIR}/include/rocksdb/kvdb_config.h"
  )

# build type dependent flags for Linux
set(CXX_FLAGS_DEBUG_OPTIONS "-g -D_GNU_SOURCE -D__DEBUG")
set(CXX_FLAGS_RELEASE_OPTIONS  "-g -O2")

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${CXX_FLAGS_DEBUG_OPTIONS}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${CXX_FLAGS_RELEASE_OPTIONS}")

set(COMMON_LIB -fPIC -Wall)

set(LINK_LIBS ${LINK_ASAN} ${LINK_THIRDPARTY_MALLOC} -pthread -Wl,--no-as-needed -fPIC -lrt -L. -lkvdb -l -L${INSDBDIR}/insdb/insdb-master/out-shared -linsdb -march=native ${LINK_FOLLY})
set(LINK_LIBS_STATIC ${LINK_ASAN} ${LINK_THIRDPARTY_MALLOC} -pthread -Wl,--no-as-needed -fPIC -lrt -L. -lkvdb_static -L${INSDBDIR}/insdb/insdb-master/out-static -linsdb -L${INSDBDIR}/snappy/google-snappy-b02bfa7/build -lsnappy ${LINK_FOLLY} -march=native)

## add "-O2" back for release
set(kvdb_CFLAGS "-MMD -MP -DLINUX -D_FILE_OFFSET_BITS=64 -fPIC -march=native")
set(kvdb_CXXFLAGS "-g -MMD -MP -DLINUX -D_FILE_OFFSET_BITS=64 -fPIC -march=native")

if(CMAKE_SYSTEM_NAME MATCHES "Cygwin")
  add_definitions(-fno-builtin-memcmp -DCYGWIN)
elseif(CMAKE_SYSTEM_NAME MATCHES "Darwin")
  add_definitions(-DOS_MACOSX)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES arm)
    add_definitions(-DIOS_CROSS_COMPILE -DROCKSDB_LITE)
    # no debug info for IOS, that will make our library big
    add_definitions(-DNDEBUG)
  endif()
elseif(CMAKE_SYSTEM_NAME MATCHES "Linux")
  add_definitions(-DOS_LINUX)
elseif(CMAKE_SYSTEM_NAME MATCHES "SunOS")
  add_definitions(-DOS_SOLARIS)
elseif(CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
  add_definitions(-DOS_FREEBSD)
elseif(CMAKE_SYSTEM_NAME MATCHES "NetBSD")
  add_definitions(-DOS_NETBSD)
elseif(CMAKE_SYSTEM_NAME MATCHES "OpenBSD")
  add_definitions(-DOS_OPENBSD)
elseif(CMAKE_SYSTEM_NAME MATCHES "DragonFly")
  add_definitions(-DOS_DRAGONFLYBSD)
elseif(CMAKE_SYSTEM_NAME MATCHES "Android")
  add_definitions(-DOS_ANDROID)
elseif(CMAKE_SYSTEM_NAME MATCHES "Windows")
  add_definitions(-DWIN32 -DOS_WIN -D_MBCS -DWIN64 -DNOMINMAX)
endif()

if(NOT WIN32)
  add_definitions(-DROCKSDB_PLATFORM_POSIX -DROCKSDB_LIB_IO_POSIX)
endif()

add_definitions("-DROCKSDB_DEFAULT_TO_ADAPTIVE_MUTEX -DROCKSDB_MALLOC_USABLE_SIZE -DROCKSDB_MALLOC_USABLE_SIZE -DROCKSDB_SCHED_GETCPU_PRESENT")
 
include_directories (BEFORE ${INSDBDIR}/insdb/insdb-master/include)
include_directories (${CMAKE_SOURCE_DIR}/include)
include_directories (${CMAKE_SOURCE_DIR}/kvdb)
include_directories (${CMAKE_SOURCE_DIR})
include_directories (${INSDBDIR}/insdb/insdb-master)


# SOURCE CODE
SET(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/builder.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/column_family.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/compaction.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/compaction_iterator.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/compaction_picker_universal.cc  
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/compaction_picker.cc  
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/dbformat.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/db_impl.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/db_iter.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/event_helpers.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/file_indexer.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/insdb_wrapper.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/internal_stats.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/log_reader.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/log_writer.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/memtable.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/memtable_list.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/merge_helper.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/merge_operator.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/range_del_aggregator.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/table_cache.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/table_properties_collector.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/version_builder.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/version_edit.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/version_set.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/write_batch_base.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/write_batch.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/write_controller.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/cache/clock_cache.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/cache/lru_cache.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/env/env.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/env/env_hdfs.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/env/env_posix.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/env/io_posix.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/memtable/alloc_tracker.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/memtable/hash_cuckoo_rep.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/memtable/hash_linklist_rep.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/memtable/hash_skiplist_rep.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/memtable/skiplistrep.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/memtable/vectorrep.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/memtable/write_buffer_manager.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/monitoring/histogram.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/monitoring/instrumented_mutex.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/monitoring/iostats_context.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/monitoring/perf_context.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/monitoring/perf_level.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/monitoring/statistics.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/monitoring/thread_status_impl.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/monitoring/thread_status_updater.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/monitoring/thread_status_util_debug.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/monitoring/thread_status_util.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/port/port_posix.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/port/stack_trace.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/table/block_based_filter_block.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/table/block_based_table_builder.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/table/block_based_table_factory.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/table/block_based_table_reader.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/table/block_builder.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/table/block_prefix_index.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/table/block.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/table/bloom_block.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/table/flush_block_policy.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/table/format.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/table/full_filter_block.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/table/get_context.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/table/index_builder.cc   
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/table/iterator.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/table/merging_iterator.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/table/meta_blocks.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/table/partitioned_filter_block.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/table/persistent_cache_helper.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/table/plain_table_builder.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/table/plain_table_factory.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/table/plain_table_index.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/table/plain_table_key_coding.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/table/plain_table_reader.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/table/sst_file_writer.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/table/table_properties.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/table/two_level_iterator.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/options/cf_options.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/options/db_options.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/options/options.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/options/options_helper.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/options/options_parser.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/options/options_sanity_check.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/util/arena.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/util/auto_roll_logger.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/util/bloom.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/util/coding.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/util/comparator.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/util/concurrent_arena.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/util/crc32c.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/util/dynamic_bloom.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/util/event_logger.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/util/filename.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/util/file_reader_writer.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/util/filter_policy.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/util/hash.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/util/log_buffer.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/util/murmurhash.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/util/random.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/util/rate_limiter.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/util/slice.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/util/status.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/util/status_message.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/util/string_util.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/util/sync_point.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/util/testutil.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/util/thread_local.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/util/threadpool_imp.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/util/transaction_test_util.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/util/xxhash.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/utilities/env_mirror.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/utilities/options/options_util.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/utilities/checkpoint/checkpoint_impl.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/utilities/memory/memory_util.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/utilities/merge_operators/max.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/utilities/merge_operators/put.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/utilities/merge_operators/uint64add.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/utilities/merge_operators/string_append/stringappend.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/utilities/merge_operators/string_append/stringappend2.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/utilities/persistent_cache/block_cache_tier.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/utilities/simulator_cache/sim_cache.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/utilities/transactions/pessimistic_transaction_db.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/utilities/transactions/pessimistic_transaction.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/utilities/transactions/transaction_base.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/utilities/transactions/transaction_db_mutex_impl.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/utilities/transactions/transaction_lock_mgr.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/utilities/transactions/transaction_util.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/utilities/transactions/write_prepared_txn.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/utilities/write_batch_with_index/write_batch_with_index_internal.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/utilities/write_batch_with_index/write_batch_with_index.cc
)

SET(HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/db/db_impl.h
)

SET(APP_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rocksdb/c.h
)

SET(REMOVED
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${kvdb_CFLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${kvdb_CXXFLAGS}")

#option(WITH_LATENCY_MODEL "Enable latency modeling" ON)
#if(WITH_LATENCY_MODEL)
#        add_definitions(-DKV_EMUL_LATENCY_MATCH)
#        message("Latency model is enabled")
#endif(WITH_LATENCY_MODEL)


#
# library build rules (shared)
#
add_library(kvdb_static STATIC ${SOURCES} ${HEADERS})
add_library(kvdb SHARED ${SOURCES} ${HEADERS})

#configure_file(${CMAKE_CURRENT_SOURCE_DIR}/xx.conf ${CMAKE_CURRENT_BINARY_DIR}/xx.conf COPYONLY)

#
# test
#
add_executable(test_kvdb ${CMAKE_CURRENT_SOURCE_DIR}/test/test_kvdb.cc ${HEADERS})
target_link_libraries(test_kvdb ${LINK_LIBS_STATIC})
add_dependencies(test_kvdb kvdb_static)

add_executable(db_bench ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/tools/db_bench.cc ${CMAKE_CURRENT_SOURCE_DIR}/kvdb/tools/db_bench_tool.cc  ${HEADERS})
target_link_libraries(db_bench ${LINK_LIBS_STATIC} ${PROJECT_SOURCE_DIR}/gflags-v2.2.1/build/lib/libgflags.a -lgflags)
add_dependencies(db_bench kvdb_static)


set(CMAKE_INSTALL_PREFIX release)

# for make install
#install (FILES  ${CMAKE_CURRENT_SOURCE_DIR}/include/kvs_adi.h DESTINATION include)
#install (FILES  ${CMAKE_CURRENT_SOURCE_DIR}/include/kvbench.h DESTINATION include)
#install (FILES  ${CMAKE_CURRENT_SOURCE_DIR}/test/kvbench.cpp DESTINATION .)
#install (FILES  ${CMAKE_CURRENT_SOURCE_DIR}/test/sample_poll.cpp DESTINATION .)
#install (FILES  ${CMAKE_CURRENT_BINARY_DIR}/kvssd_emul.conf DESTINATION . RENAME kvssd_emul.conf)
#install (FILES  ${CMAKE_CURRENT_SOURCE_DIR}/src/Makefile_USER DESTINATION . RENAME Makefile)
#install (FILES  ${CMAKE_CURRENT_SOURCE_DIR}/src/README_USER DESTINATION . RENAME README)
#install (FILES  ${CMAKE_CURRENT_SOURCE_DIR}/src/RUN_USER PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ DESTINATION . RENAME run.sh)
#install (TARGETS kvdb kvdb_static DESTINATION lib)


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
  #COMMAND ${CMAKE_COMMAND} --build . --target install
  #COMMAND ${CMAKE_COMMAND} --build . --target all
  COMMENT "Switch CMAKE_BUILD_TYPE to Debug"
)
