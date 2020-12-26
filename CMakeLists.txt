cmake_minimum_required(VERSION 3.16)

project("TrivialThreadPool" VERSION 0.0.1 LANGUAGES CXX)

add_library(${PROJECT_NAME} INTERFACE)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME}
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_20)

option(BUILD_TESTING "Build tests" OFF)
if(BUILD_TESTING)

    set(SANITAZE "thread" CACHE STRING "Specify -DSANITAZE=[address|thread|undefined|leak]")

    target_compile_options(${PROJECT_NAME}
        INTERFACE
            $<$<CONFIG:COVERAGE>:--coverage -g -O0>
            $<$<CONFIG:SANITAZE>:-fsanitize=${SANITAZE} -g -O2>
    )

    target_link_libraries(${PROJECT_NAME}
        INTERFACE
            $<$<CONFIG:COVERAGE>:--coverage>
            $<$<CONFIG:SANITAZE>:-fsanitize=${SANITAZE}>
    )

    add_subdirectory(third_party/Catch2)
    add_subdirectory(test)

endif()
