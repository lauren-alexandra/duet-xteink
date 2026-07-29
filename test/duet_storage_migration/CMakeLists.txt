add_executable(DuetStorageMigrationTest
  DuetStorageMigrationTest.cpp
  ${REPO_ROOT}/lib/DuetStorage/DuetStorageMigrationCore.cpp
  ${REPO_ROOT}/lib/DuetStorage/DuetStoragePaths.cpp
)

target_include_directories(DuetStorageMigrationTest PRIVATE
  ${REPO_ROOT}/lib/DuetStorage
)

target_link_libraries(DuetStorageMigrationTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(DuetStorageMigrationTest)
