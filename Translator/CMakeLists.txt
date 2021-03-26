cmake_minimum_required(VERSION 3.19)
set(PROJECT_NAME WFA_Translator)
set(CMAKE_CXX_STANDARD 17)

project(${PROJECT_NAME})

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})


add_executable(${PROJECT_NAME} src/Source.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE include)
add_subdirectory(src)
add_subdirectory(include)
target_include_directories(${PROJECT_NAME} PRIVATE ../Shared/include)
add_subdirectory(../Shared/include build)


include(CMakeGroupSources.txt)

find_package(CURL CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl)

#Dependencies for uWebSockets

#uSockets is not a CMAKE library, so must be manually inlcuded
find_library(USOCKETS_LIB REQUIRED NAMES uSockets)
if(NOT USOCKETS_LIB)
  message(FATAL_ERROR "uSockets (not uWebSockets) library not found")
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE ${USOCKETS_LIB})

find_package(unofficial-libuv CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE unofficial::libuv::libuv)
target_link_libraries(${PROJECT_NAME} PRIVATE ZLIB::ZLIB)
target_link_libraries(${PROJECT_NAME} PRIVATE OpenSSL::SSL OpenSSL::Crypto)

find_path(UWEBSOCKETS_INCLUDE_DIRS "uwebsockets/App.h")
target_include_directories(${PROJECT_NAME} PRIVATE ${UWEBSOCKETS_INCLUDE_DIRS})
