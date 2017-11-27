cmake_minimum_required(VERSION 2.8)
add_library (CanaryDyckAA STATIC DyckAliasAnalysis.cpp AAAnalyzer.cpp EdgeLabel.cpp ProgressBar.cpp)
set_target_properties (CanaryDyckAA PROPERTIES FOLDER "Canary")
include_directories (${INCLUDE_DIR}/DyckAA)
