file(GLOB_RECURSE sources CONFIGURE_DEPENDS "*.cpp")
add_executable(canary ${sources})
target_compile_options(canary PUBLIC -fno-rtti)
if(LLVM_LINK_LLVM_DYLIB)
    set(llvm_libs LLVM)
else()
    llvm_map_components_to_libnames(
            llvm_libs bitreader bitwriter asmparser irreader instrumentation scalaropts objcarcopts ipo vectorize ${LLVM_ALL_TARGETS} codegen)
endif()
target_link_libraries(canary CanaryDyckAA CanaryTransformer CanaryCallGraph CanaryAnnotation CanaryDyckGraph ${llvm_libs})