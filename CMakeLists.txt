cmake_minimum_required(VERSION 3.17)

project(FanControl LANGUAGES C)

# C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Warnings (optional but recommended)
add_compile_options(-Wall -Wextra -Wpedantic)

# Main executable
add_library(fan-curve STATIC src/fan_curve.c)
target_include_directories(fan-curve PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_executable(fan-control src/main.c)

# Link libm the modern CMake way (instead of stuffing -lm into C flags)
target_link_libraries(fan-control PRIVATE fan-curve m)

include(CTest)
if(BUILD_TESTING)
  add_executable(test-fan-curve tests/test_fan_curve.c)
  target_link_libraries(test-fan-curve PRIVATE fan-curve)
  add_test(NAME fan-curve COMMAND test-fan-curve)

  add_executable(test-cli tests/test_cli.c)
  target_link_libraries(test-cli PRIVATE fan-curve m)
  add_test(NAME cli COMMAND test-cli)
endif()

# Install rules
include(GNUInstallDirs)
install(
  TARGETS fan-control
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  # Typical executable permissions: 755 (owner rwx, group rx, world rx)
  PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
              GROUP_READ GROUP_EXECUTE
              WORLD_READ WORLD_EXECUTE
)
