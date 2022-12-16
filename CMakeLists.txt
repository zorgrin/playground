cmake_minimum_required(VERSION 3.17)

project(uring LANGUAGES CXX)

if(NOT (CMAKE_MAJOR_VERSION LESS 3) AND NOT (CMAKE_MINOR_VERSION LESS 12)) # version >= 3.12
    cmake_policy(SET CMP0074 NEW) # to enable <package>_ROOT variables due to new 3.12 policy
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CheckIncludeFileCXX)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR}/cmake)

if(TESTBED_OUTPUT_DIR)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${TESTBED_OUTPUT_DIR})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${TESTBED_OUTPUT_DIR})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${TESTBED_OUTPUT_DIR})
endif()

add_compile_options(-Wall)
add_compile_options(-Wextra)
add_compile_options(-Werror=return-type)
add_compile_options(-Werror=uninitialized)
add_compile_options(-Werror=init-self)
add_compile_options(-Werror=conversion-null)
add_compile_options(-Werror=parentheses)
add_compile_options(-Werror=sign-compare)
add_compile_options(-Werror=switch)
add_compile_options(-Werror=reorder)
add_compile_options(-Werror=enum-compare)
add_compile_options(-Werror=array-bounds)
add_compile_options(-Werror=overflow)
add_compile_options(-Werror=unused-result)
add_compile_options(-Werror=conversion)
add_compile_options(-Werror=type-limits)
add_compile_options(-Werror=missing-field-initializers)
add_compile_options(-Werror=terminate)

include(CheckCXXCompilerFlag)

check_cxx_compiler_flag("-Werror=integer-overflow" COMPILER_FEATURES_INTEGER_OVERFLOW_WARNING_SUPPORTED)
if(COMPILER_FEATURES_INTEGER_OVERFLOW_WARNING_SUPPORTED)
    add_compile_options(-Werror=integer-overflow)
endif()

check_cxx_compiler_flag("-Werror=int-in-bool-context" COMPILER_FEATURES_INT_IN_BOOL_CONTEXT_WARNING_SUPPORTED)
if(COMPILER_FEATURES_INT_IN_BOOL_CONTEXT_WARNING_SUPPORTED)
    add_compile_options(-Werror=int-in-bool-context)
endif()

check_cxx_compiler_flag("-Werror=mismatched-tags" COMPILER_FEATURES_MISMATCHED_TAGS_WARNING_SUPPORTED)
if(COMPILER_FEATURES_MISMATCHED_TAGS_WARNING_SUPPORTED)
    add_compile_options(-Werror=mismatched-tags)
endif()

check_cxx_compiler_flag("-Werror=inconsistent-missing-override" COMPILER_FEATURES_INCONISTENT_MISSING_OVERRIDE_SUPPORTED)
if(COMPILER_FEATURES_INCONISTENT_MISSING_OVERRIDE_SUPPORTED)
    add_compile_options(-Werror=inconsistent-missing-override)
endif()

enable_testing()

add_subdirectory(src)
