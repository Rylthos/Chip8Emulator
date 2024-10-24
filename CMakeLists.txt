cmake_minimum_required(VERSION 3.18)

project(Chip8 C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB_RECURSE project_files CONFIGURE_DEPENDS "src/*.c")
add_executable(${PROJECT_NAME} ${project_files})

target_link_libraries(${PROJECT_NAME} "-g -lm -lncurses")
