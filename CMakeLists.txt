cmake_minimum_required(VERSION 3.21)
project(BetterGroup LANGUAGES CXX)

include(FetchContent)
FetchContent_Declare(
  truckersmp_sdk
  URL https://github.com/TruckersMP/GameClientSDK/archive/refs/heads/master.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(truckersmp_sdk)

set(BETTERGROUP_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/generated/Main.cxx")
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/src/Main.cxx
    -DOUTPUT=${BETTERGROUP_SOURCE}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateRc33.cmake
  RESULT_VARIABLE BETTERGROUP_GENERATE_RESULT
)

if(NOT BETTERGROUP_GENERATE_RESULT EQUAL 0)
  message(FATAL_ERROR "Failed to generate BetterGroup RC3.3 source")
endif()

add_library(BetterGroup SHARED "${BETTERGROUP_SOURCE}")
target_compile_features(BetterGroup PRIVATE cxx_std_17)
target_include_directories(BetterGroup PRIVATE "${truckersmp_sdk_SOURCE_DIR}/include")
target_compile_definitions(BetterGroup PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(BetterGroup PRIVATE d3d11 d3dcompiler winhttp shell32 ole32)
