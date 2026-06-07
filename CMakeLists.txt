cmake_minimum_required(VERSION 3.16)
project(RemoteJournal CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSSH   REQUIRED libssh)
pkg_check_modules(SQLITE3  REQUIRED sqlite3)
pkg_check_modules(JSONCPP  REQUIRED jsoncpp)

include_directories(
    include
    ${LIBSSH_INCLUDE_DIRS}
    ${SQLITE3_INCLUDE_DIRS}
    ${JSONCPP_INCLUDE_DIRS}
)

set(SOURCES
    src/Events.cpp
    src/LogEntry.cpp
    src/Database.cpp
    src/SSHJournalReader.cpp
    src/ConnectionProfile.cpp
    src/FilterConfig.cpp
    src/AutoStart.cpp
    src/LogTableCtrl.cpp
    src/ProfileDialog.cpp
    src/FilterConfigDialog.cpp
    src/MainFrame.cpp
)

add_executable(RemoteJournal ${SOURCES})

target_link_libraries(RemoteJournal
    ${wxWidgets_LIBRARIES}
    ${LIBSSH_LIBRARIES}
    ${SQLITE3_LIBRARIES}
    ${JSONCPP_LIBRARIES}
)

target_compile_options(RemoteJournal PRIVATE
    -Wall -Wextra -Wno-deprecated-declarations)
