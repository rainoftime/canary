cmake_minimum_required(VERSION 2.8)
add_library (CanaryAnnotation STATIC LibcAnnotation.cpp)
set_target_properties (CanaryAnnotation PROPERTIES FOLDER "Canary")
