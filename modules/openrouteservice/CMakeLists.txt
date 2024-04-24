cmake_minimum_required(VERSION 3.10)
project(motis)

include_directories(include)

file(GLOB_RECURSE motis-openrouteservice-files src/*.cc)

add_library(motis-openrouteservice STATIC ${motis-openrouteservice-files})
target_include_directories(motis-openrouteservice PUBLIC include)
target_compile_features(motis-openrouteservice PUBLIC cxx_std_17)
target_link_libraries(motis-openrouteservice
  motis-core
  motis-module
)
target_compile_options(motis-openrouteservice PRIVATE ${MOTIS_CXX_FLAGS})
