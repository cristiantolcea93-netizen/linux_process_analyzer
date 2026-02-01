cmake_minimum_required(VERSION 3.10)

# Set some basic project attributes
project (process_analyzer
	VERSION 1.0
	DESCRIPTION "linux process analyzer")

# This project will output an executable file
add_executable(${PROJECT_NAME}
    process_analyzer.c
    process_snapshot/process_snapshot.c
    args_parser/args_parser.c
    process_stats/process_stats.c
    config/config.c
)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic -Werror)

# header include path
target_include_directories(${PROJECT_NAME} PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/process_snapshot"
    "${CMAKE_CURRENT_SOURCE_DIR}/args_parser"
    "${CMAKE_CURRENT_SOURCE_DIR}/process_stats"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/uthash"
    "${CMAKE_CURRENT_SOURCE_DIR}/config"
)

set_target_properties(${PROJECT_NAME}
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
