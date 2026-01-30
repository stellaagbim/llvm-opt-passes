cmake_minimum_required(VERSION 3.20)
project(LLVMOptPasses VERSION 1.0.0 LANGUAGES CXX)

# LLVM 15+ required (New Pass Manager)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find LLVM installation
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "LLVM include dirs: ${LLVM_INCLUDE_DIRS}")
message(STATUS "LLVM library dir: ${LLVM_LIBRARY_DIR}")

# Verify LLVM version compatibility
if(LLVM_PACKAGE_VERSION VERSION_LESS "15.0")
    message(FATAL_ERROR "LLVM 15.0 or higher required (New Pass Manager)")
endif()

# Add LLVM definitions and include paths
include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

# Include our headers
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Source files for our passes
set(PASS_SOURCES
    src/ConstantFoldingPass.cpp
    src/LoopUnrollingPass.cpp
    src/RedundancyAnalysis.cpp
    src/RedundancyEliminationPass.cpp
    src/PassRegistration.cpp
)

# Build shared library plugin
add_library(LLVMOptPasses MODULE ${PASS_SOURCES})

# Set plugin properties
set_target_properties(LLVMOptPasses PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    PREFIX ""  # Remove 'lib' prefix on Unix
)

# Platform-specific settings
if(APPLE)
    set_target_properties(LLVMOptPasses PROPERTIES
        LINK_FLAGS "-undefined dynamic_lookup"
    )
elseif(WIN32)
    # Windows needs explicit LLVM linking
    llvm_map_components_to_libnames(LLVM_LIBS
        Core
        Support
        Analysis
        TransformUtils
        Scalar
    )
    target_link_libraries(LLVMOptPasses PRIVATE ${LLVM_LIBS})
endif()

# Compiler warnings
target_compile_options(LLVMOptPasses PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter  # LLVM APIs have many unused params
)

# Debug/Release configurations
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(LLVMOptPasses PRIVATE DEBUG_PASSES)
    target_compile_options(LLVMOptPasses PRIVATE -g -O0)
else()
    target_compile_options(LLVMOptPasses PRIVATE -O2)
endif()

#===============================================================================
# Installation
#===============================================================================

install(TARGETS LLVMOptPasses
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

#===============================================================================
# Testing Support
#===============================================================================

enable_testing()

# Custom target to run tests
add_custom_target(check
    COMMAND ${CMAKE_COMMAND} -E echo "Running LLVM pass tests..."
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_tests.sh 
            ${CMAKE_BINARY_DIR}/LLVMOptPasses${CMAKE_SHARED_MODULE_SUFFIX}
    DEPENDS LLVMOptPasses
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# Benchmark target
add_custom_target(benchmark
    COMMAND ${CMAKE_COMMAND} -E echo "Running benchmarks..."
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/benchmark.sh
            ${CMAKE_BINARY_DIR}/LLVMOptPasses${CMAKE_SHARED_MODULE_SUFFIX}
    DEPENDS LLVMOptPasses
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

#===============================================================================
# Documentation
#===============================================================================

find_package(Doxygen)
if(DOXYGEN_FOUND)
    set(DOXYGEN_GENERATE_HTML YES)
    set(DOXYGEN_EXTRACT_ALL YES)
    doxygen_add_docs(docs
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        COMMENT "Generating API documentation"
    )
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "Configuration Summary:")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  LLVM version: ${LLVM_PACKAGE_VERSION}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "")
