cmake_minimum_required(VERSION 3.20)

project(backend-log)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED libsystemd)


add_executable(${PROJECT_NAME}
    src/main.cpp
    ${SYSTEMD_INCLUDE_DIRS}
)

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME}
    Threads::Threads
    ${SYSTEMD_LIBRARIES}
)