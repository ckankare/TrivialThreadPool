cmake_minimum_required(VERSION 3.16)

find_package(Threads)

set(sources "tests.cpp")

add_executable(unit_tests ${sources})

target_link_libraries(unit_tests ${PROJECT_NAME}::${PROJECT_NAME} Threads::Threads Catch2::Catch2WithMain)
target_compile_options(unit_tests
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -pedantic -Werror>
)
