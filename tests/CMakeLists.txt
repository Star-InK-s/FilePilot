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

add_executable(filepilot_backup_phase1_tests
    BackupPhase1Tests.cpp
    BackupPhase1Tests.h
)
target_link_libraries(filepilot_backup_phase1_tests
    PRIVATE
        filepilot_core
        Qt6::Test
)
filepilot_enable_warnings(filepilot_backup_phase1_tests)

foreach(backup_test_class
        FileSnapshotContractTest
        FileHasherContractTest
        BackupPlanContractTest
        BackupInventoryBuilderContractTest
        BackupPrevalidatorContractTest)
    string(TOLOWER "${backup_test_class}" backup_test_name)
    add_test(
        NAME "filepilot_backup_phase1_${backup_test_name}"
        COMMAND filepilot_backup_phase1_tests
    )
    set_tests_properties("filepilot_backup_phase1_${backup_test_name}" PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;FILEPILOT_BACKUP_TEST_CLASS=${backup_test_class}"
    )
endforeach()

add_executable(filepilot_backup_executor_phase1_tests
    BackupExecutorPhase1Tests.cpp
    BackupExecutorPhase1Tests.h
)
target_link_libraries(filepilot_backup_executor_phase1_tests
    PRIVATE
        filepilot_core
        Qt6::Test
)
filepilot_enable_warnings(filepilot_backup_executor_phase1_tests)

add_test(
    NAME filepilot_backup_executor_phase1_contract
    COMMAND filepilot_backup_executor_phase1_tests
)
set_tests_properties(filepilot_backup_executor_phase1_contract PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;FILEPILOT_BACKUP_EXECUTOR_TEST_CLASS=BackupExecutorContractTest"
)

add_executable(filepilot_backup_directory_phase1_tests
    BackupDirectoryPhase1Tests.cpp
    BackupDirectoryPhase1Tests.h
)
target_link_libraries(filepilot_backup_directory_phase1_tests
    PRIVATE
        filepilot_core
        Qt6::Test
)
filepilot_enable_warnings(filepilot_backup_directory_phase1_tests)

add_test(
    NAME filepilot_backup_directory_phase1_contract
    COMMAND filepilot_backup_directory_phase1_tests
)
set_tests_properties(filepilot_backup_directory_phase1_contract PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;FILEPILOT_BACKUP_DIRECTORY_TEST_CLASS=BackupDirectoryPhase1ContractTest"
)