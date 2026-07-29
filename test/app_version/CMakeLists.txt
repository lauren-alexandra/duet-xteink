add_executable(AppVersionTest
  AppVersionTest.cpp
)

target_include_directories(AppVersionTest PRIVATE
  ${REPO_ROOT}
  ${REPO_ROOT}/lib
)

target_link_libraries(AppVersionTest PRIVATE
  crosspoint_test_common
)

add_test(NAME AppVersionTest COMMAND AppVersionTest)
