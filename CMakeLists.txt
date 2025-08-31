cmake_minimum_required(VERSION 3.17)

project(FanControl LANGUAGES C)

# C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Warnings (optional but recommended)
add_compile_options(-Wall -Wextra -Wpedantic)

# Main executable
add_executable(fan-control src/main.c)

# Link libm the modern CMake way (instead of stuffing -lm into C flags)
target_link_libraries(fan-control PRIVATE m)

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
