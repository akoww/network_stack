# macro: Add Individual Test Executables
# This macro creates a separate executable for each test file provided.
macro(add_separate_tests test_files_var)
    foreach(test_src IN LISTS ${test_files_var})
        get_filename_component(test_name ${test_src} NAME_WE)
        
        add_executable(${test_name} ${test_src})
        
        target_include_directories(${test_name} PRIVATE 
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        
        target_link_libraries(${test_name} PRIVATE 
            GTest::gtest 
            GTest::gtest_main 
            network_stack_core
        )
        
        target_compile_definitions(${test_name} PRIVATE
            ASIO_STANDALONE=1
            _HAS_STD_BYTE=0
        )

        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endmacro()