cmake_minimum_required(VERSION 3.12.0)
project(DOOMEternalMacro VERSION 1.4.0 LANGUAGES CXX)

add_executable(DOOMEternalMacro main.cpp)

target_compile_features(DOOMEternalMacro PRIVATE cxx_std_17)

if(MSVC)
    target_compile_definitions(DOOMEternalMacro PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

install(TARGETS DOOMEternalMacro
    RUNTIME DESTINATION .
)

install(FILES bindings.txt
    DESTINATION .
)
