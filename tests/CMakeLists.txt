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
set(FILEPILOT_DPI_SCALES
    "100:1"
    "125:1.25"
    "150:1.5"
    "200:2"
)

foreach(entry IN LISTS FILEPILOT_DPI_SCALES)
    string(REPLACE ":" ";" parts "${entry}")
    list(GET parts 0 dpi_percent)
    list(GET parts 1 scale_factor)
    set(test_name "filepilot_dpi_${dpi_percent}")
    add_test(
        NAME ${test_name}
        COMMAND filepilot_tests
    )
    set_tests_properties(${test_name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=${scale_factor};FILEPILOT_TEST_CLASS=DpiLayoutTest"
    )
endforeach()
