cmake_minimum_required(VERSION 3.15)
project(FileIntegrityMonitor)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(FileMonitor main.cpp FileMonitor.cpp)
target_link_libraries(FileMonitor comdlg32 shell32 ole32 advapi32)

enable_testing()

include(FetchContent)
FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
)
FetchContent_MakeAvailable(doctest)

add_executable(FileMonitorTests tests.cpp FileMonitor.cpp)
target_link_libraries(FileMonitorTests doctest::doctest comdlg32 shell32 ole32 advapi32)

add_test(NAME AllTests COMMAND FileMonitorTests)