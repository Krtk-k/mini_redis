# Set up cmake
cmake_minimum_required(VERSION 3.22)
project(mini_redis VERSION 1.0)

#set up c++ standards
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

#forces compiler to tell for any risky code
#compiler flags are very necessary becuase they help in debugging
set(COMPILER_FLAGS 
"-Wall" "-Wextra" "-Wshadow" "-Wnon-virtual-dtor" "-pedantic" "-Werror"
)

add_compile_options(${COMPILER_FLAGS})

#create executable
add_executable(server src/server.cpp)

target_include_directories(server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)