cmake_minimum_required(VERSION 2.8)
file(GLOB_RECURSE sources CONFIGURE_DEPENDS "*.cpp")
add_library (CanaryTransformer STATIC ${sources})
set_target_properties (CanaryTransformer PROPERTIES FOLDER "Canary")