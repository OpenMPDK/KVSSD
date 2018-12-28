#test
add_executable(testc test.c)
target_link_libraries(testc blkin lttng-ust)
add_test(NAME testc COMMAND $<TARGET_FILE:testc>)

#testpp
add_executable(testpp test.cc)
set_target_properties(testpp PROPERTIES COMPILE_FLAGS "-std=c++11")
target_link_libraries(testpp blkin lttng-ust pthread)
add_test(NAME testpp COMMAND $<TARGET_FILE:testpp>)

#testppp
add_executable(testppp test_p.cc)
target_link_libraries(testppp blkin lttng-ust)
add_test(NAME testppp COMMAND $<TARGET_FILE:testppp>)
