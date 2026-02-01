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
