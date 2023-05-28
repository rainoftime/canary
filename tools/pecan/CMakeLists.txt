file(GLOB_RECURSE sources CONFIGURE_DEPENDS "*.cpp")
add_executable(pecan ${sources})
target_link_libraries(pecan ${llvm_libs})