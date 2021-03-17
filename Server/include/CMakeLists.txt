cmake_minimum_required(VERSION 3.1)

#Automatically generated from files in this directory.
target_sources(${PROJECT_NAME} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/Database.h")
target_sources(${PROJECT_NAME} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/Network.h")
target_sources(${PROJECT_NAME} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/ServerData.h")

#Automatically generated from subdirectories in this directory.
add_subdirectory("WebRoutes")
