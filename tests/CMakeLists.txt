cmake_minimum_required(VERSION 3.10)

project(process_analyzer_tests C)

enable_testing()


# ------------------------
# Unity
# ------------------------

add_library(unity
    unity/src/unity.c
)

target_include_directories(unity PUBLIC
    unity/src
)


# ------------------------
# Test executable
# ------------------------

add_executable(test_config
    test_config.c
    ../code/config/config.c
)


target_include_directories(test_config PRIVATE
    ../code/config
    unity/src
)


target_link_libraries(test_config
    unity
)


# ------------------------
# Register test
# ------------------------

add_test(
    NAME test_config
    COMMAND test_config
)

add_executable(test_process_snapshot
    test_process_snapshot.c
)

target_include_directories(test_process_snapshot PRIVATE
    ../code/process_snapshot
    ../code/process_stats
    ../code/config
    ../code/args_parser
    unity/src
)

target_link_libraries(test_process_snapshot
    unity
)

add_test(
    NAME test_process_snapshot
    COMMAND test_process_snapshot
)
