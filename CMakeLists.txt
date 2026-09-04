cmake_minimum_required(VERSION 3.21)
project(BetterGroup LANGUAGES CXX)

include(FetchContent)
FetchContent_Declare(
  truckersmp_sdk
  URL https://github.com/TruckersMP/GameClientSDK/archive/refs/heads/master.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(truckersmp_sdk)

add_library(BetterGroup SHARED src/Main.cxx)
target_compile_features(BetterGroup PRIVATE cxx_std_17)
target_include_directories(BetterGroup PRIVATE "${truckersmp_sdk_SOURCE_DIR}/include")
target_compile_definitions(BetterGroup PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(BetterGroup PRIVATE d3d11 d3dcompiler winhttp shell32 ole32)
