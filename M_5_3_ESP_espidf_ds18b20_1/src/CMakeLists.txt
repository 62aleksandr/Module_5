# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

file(GLOB app_sources "*.c")

idf_component_register(SRCS ${app_sources}
    INCLUDE_DIRS "."  
    REQUIRES onewire_bus ds18b20)
