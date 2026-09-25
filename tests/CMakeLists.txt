add_executable(filepilot_tests
    ExecutionTestCases.cpp
    ExecutionTestCases.h
    TestCases.h
    test_filepilot.cpp
)
target_link_libraries(filepilot_tests
    PRIVATE
        filepilot_gui
        Qt6::Test
)

filepilot_enable_warnings(filepilot_tests)

add_test(
    NAME filepilot_tests
    COMMAND filepilot_tests
)

set_tests_properties(filepilot_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)