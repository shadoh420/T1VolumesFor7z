cmake_minimum_required(VERSION 3.20)
project(TribesVol7z VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TRIBESVOL_LEGACY_P7ZIP "Build Linux .so with virtual ~IUnknown() for legacy p7zip (< v23)" OFF)

set(TRIBESVOL_SOURCES
    src/DllMain.cpp
    src/DarkstarCodec.cpp
    src/VolFormat.cpp
    src/VolHandler.cpp
)

if(WIN32)
    list(APPEND TRIBESVOL_SOURCES
        src/TribesVol.def
        src/TribesVol.rc
    )

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        if(CMAKE_GENERATOR_PLATFORM MATCHES "ARM64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64")
            set(TRIBESVOL_ARCH_SUFFIX "arm64")
        else()
            set(TRIBESVOL_ARCH_SUFFIX "64")
        endif()
    else()
        set(TRIBESVOL_ARCH_SUFFIX "32")
    endif()
    set(TRIBESVOL_OUTPUT_NAME "TribesVol.${TRIBESVOL_ARCH_SUFFIX}")
elseif(UNIX AND NOT APPLE)
    set(TRIBESVOL_OUTPUT_NAME "TribesVol")
else()
    message(FATAL_ERROR "TribesVol7z currently supports Windows and Linux.")
endif()

add_library(TribesVol SHARED ${TRIBESVOL_SOURCES})

set_target_properties(TribesVol PROPERTIES
    OUTPUT_NAME "${TRIBESVOL_OUTPUT_NAME}"
    PREFIX ""
)

target_compile_definitions(TribesVol PRIVATE
    TRIBESVOL_VER_MAJOR=${PROJECT_VERSION_MAJOR}
    TRIBESVOL_VER_MINOR=${PROJECT_VERSION_MINOR}
    TRIBESVOL_VER_PATCH=${PROJECT_VERSION_PATCH}
)

if(WIN32)
    target_compile_definitions(TribesVol PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    if(MSVC)
        # Static CRT so end users never need a separate VC++ Redistributable.
        set_property(TARGET TribesVol PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_compile_options(TribesVol PRIVATE /W4 /EHsc /utf-8)
    endif()
    target_link_libraries(TribesVol PRIVATE
        oleaut32
        ole32
    )
else()
    if(TRIBESVOL_LEGACY_P7ZIP)
        target_compile_definitions(TribesVol PRIVATE Z7_USE_VIRTUAL_DESTRUCTOR_IN_IUNKNOWN=1)
    endif()
    target_compile_options(TribesVol PRIVATE -Wall -Wextra -fvisibility=hidden)
endif()
