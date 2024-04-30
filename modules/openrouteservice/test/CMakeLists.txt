project(test)

add_executable(Google_Tests_run ors_one_to_many.cpp)

target_link_libraries(Google_Tests_run gtest gtest_main
        motis-openrouteservice
        motis-core
        motis-module
)