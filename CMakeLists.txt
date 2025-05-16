cmake_minimum_required(VERSION 3.14)
project(entityProject)

include(FetchContent)

# すでにダウンロード済みかチェック
if(NOT googletest_POPULATED)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
  )
  set(BUILD_GMOCK OFF CACHE BOOL "Disable building of Google Mock")
  set(INSTALL_GTEST OFF CACHE BOOL "Disable installation of Google Test")
endif()

# 🔽 必ず `endif()` の後に `FetchContent_MakeAvailable`
FetchContent_MakeAvailable(googletest)

enable_testing()  # CTestを有効化

# 🔽 **Google Testのヘッダディレクトリを追加**
include_directories(${googletest_SOURCE_DIR}/include)

# 🔽 **単体テストの定義**
add_executable(
  entityTest
  test.cpp
)
target_link_libraries(
  entityTest
  GTest::gtest_main
)

# 🔽 **Google TestはC++14以上が必要**
target_compile_features(
  entityTest
  PUBLIC
  cxx_std_14
)

include(GoogleTest)

# 🔽 **テストケースの自動検出**
gtest_discover_tests(entityTest)

