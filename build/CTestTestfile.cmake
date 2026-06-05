# CMake generated Testfile for 
# Source directory: C:/HSE/PROJECT
# Build directory: C:/HSE/PROJECT/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(AllTests "C:/HSE/PROJECT/build/FileMonitorTests.exe")
set_tests_properties(AllTests PROPERTIES  _BACKTRACE_TRIPLES "C:/HSE/PROJECT/CMakeLists.txt;23;add_test;C:/HSE/PROJECT/CMakeLists.txt;0;")
subdirs("_deps/doctest-build")
