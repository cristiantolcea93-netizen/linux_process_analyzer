cmake_minimum_required(VERSION 3.10)

# Set some basic project attributes
project (process_analyzer
	VERSION 0.1
	DESCRIPTION "A Hello World Project")

# This project will output an executable file
add_executable(${PROJECT_NAME}
    process_analyzer.c
    process_snapshot/process_snapshot.c
    args_parser/args_parser.c
)

# header include path
target_include_directories(${PROJECT_NAME} PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/process_snapshot"
    "${CMAKE_CURRENT_SOURCE_DIR}/args_parser"
)

