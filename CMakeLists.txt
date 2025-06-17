cmake_minimum_required(VERSION 3.14)
project(entityProject)

include(FetchContent)

# GoogleTestのセットアップ
if(NOT googletest_POPULATED)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
  )
  set(BUILD_GMOCK OFF CACHE BOOL "Disable building of Google Mock")
  set(INSTALL_GTEST OFF CACHE BOOL "Disable installation of Google Test")
endif()

FetchContent_MakeAvailable(googletest)

enable_testing()  # CTestを有効化

# unordered_dense を追加
include_directories(${PROJECT_SOURCE_DIR}/unordered_dense/include)

# GoogleTestのインクルード
include_directories(${googletest_SOURCE_DIR}/include)

# 実行ファイルを作成
add_executable(
  entityTest
  test.cpp
)

#  GoogleTestとリンク
target_link_libraries(
  entityTest
  GTest::gtest_main
)

# C++14 を有効化
target_compile_features(
  entityTest
  PUBLIC
  cxx_std_14
)

# GoogleTestの自動テスト検出
include(GoogleTest)
gtest_discover_tests(entityTest)