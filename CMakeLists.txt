cmake_minimum_required(VERSION 3.20)

project(
    RemoteJournal
    VERSION 1.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --------------------------------------------------------------------
# wxWidgets
# --------------------------------------------------------------------

find_package(wxWidgets REQUIRED COMPONENTS
    core
    base
)

include(${wxWidgets_USE_FILE})

# --------------------------------------------------------------------
# libssh
# --------------------------------------------------------------------

find_package(PkgConfig REQUIRED)

pkg_check_modules(LIBSSH REQUIRED libssh)

# --------------------------------------------------------------------
# Sources
# --------------------------------------------------------------------

set(SOURCES
    main.cpp
)

set(HEADERS
    RemoteJournal.h
)

# --------------------------------------------------------------------
# Executable
# --------------------------------------------------------------------

add_executable(
    RemoteJournal
    ${SOURCES}
    ${HEADERS}
)

target_include_directories(
    RemoteJournal
    PRIVATE
        include
        ${LIBSSH_INCLUDE_DIRS}
)

target_link_directories(
    RemoteJournal
    PRIVATE
        ${LIBSSH_LIBRARY_DIRS}
)

target_link_libraries(
    RemoteJournal
    PRIVATE
        ${wxWidgets_LIBRARIES}
        ${LIBSSH_LIBRARIES}
)

target_compile_options(
    RemoteJournal
    PRIVATE
        ${LIBSSH_CFLAGS_OTHER}
)

# --------------------------------------------------------------------
# Linux Threads
# --------------------------------------------------------------------

find_package(Threads REQUIRED)

target_link_libraries(
    RemoteJournal
    PRIVATE
        Threads::Threads
)

# --------------------------------------------------------------------
# Warnings
# --------------------------------------------------------------------

if(MSVC)
    target_compile_options(
        RemoteJournal
        PRIVATE
            /W4
    )
else()
    target_compile_options(
        RemoteJournal
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
endif()

# --------------------------------------------------------------------
# Installation
# --------------------------------------------------------------------

install(
    TARGETS RemoteJournal
    RUNTIME DESTINATION bin
)