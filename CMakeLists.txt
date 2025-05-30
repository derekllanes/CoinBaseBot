cmake_minimum_required(VERSION 3.28)
project(CoinBaseBot)

set(CMAKE_CXX_STANDARD 17)

# 1) Handle large object files on MSVC/GCC
if(MSVC)
    add_compile_options(/bigobj)
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wa,-mbig-obj -ffunction-sections -fdata-sections)
endif()

# 2) Find libraries from vcpkg (assuming you have installed them globally)
#    e.g. vcpkg install boost openssl curl rapidjson jwt-cpp

# Boost (for Asio or other components)
find_package(Boost REQUIRED COMPONENTS system)
if(Boost_FOUND)
    message(STATUS "Found Boost: ${Boost_INCLUDE_DIRS}")
    include_directories(${Boost_INCLUDE_DIRS})
else()
    message(FATAL_ERROR "Boost not found!")
endif()

# libcurl
find_package(CURL CONFIG REQUIRED)
if(CURL_FOUND)
    message(STATUS "Found CURL: ${CURL_INCLUDE_DIRS}")
endif()

# OpenSSL
find_package(OpenSSL REQUIRED)
if(OPENSSL_FOUND)
    message(STATUS "Found OpenSSL: ${OPENSSL_INCLUDE_DIR}")
else()
    message(WARNING "OpenSSL not found!")
endif()

# RapidJSON (header-only)
find_path(RAPIDJSON_INCLUDE_DIR rapidjson/document.h)
if(NOT RAPIDJSON_INCLUDE_DIR)
    message(FATAL_ERROR "Could not find RapidJSON! Make sure it is installed via vcpkg (e.g., 'vcpkg install rapidjson').")
else()
    message(STATUS "Found RapidJSON in: ${RAPIDJSON_INCLUDE_DIR}")
    include_directories(${RAPIDJSON_INCLUDE_DIR})
endif()

# (Optional) If you use nlohmann/json, do:
find_package(nlohmann_json CONFIG REQUIRED)

# (Optional) If you use jwt-cpp (header-only from vcpkg), no need to find_package;
# just #include <jwt-cpp/jwt.h> or <jwt-cpp/jwt.hpp> in your code.

# 3) Create the executable from your source files
add_executable(CoinBaseBot main.cpp)

# 4) Link libraries
#    - Boost libraries
#    - Curl
#    - OpenSSL
target_link_libraries(CoinBaseBot
        PRIVATE
        ${Boost_LIBRARIES}
        CURL::libcurl
        OpenSSL::SSL
        OpenSSL::Crypto
        ws2_32  # On Windows for sockets
        nlohmann_json::nlohmann_json
)

# 5) If you want precompiled headers, you can still do:
# target_precompile_headers(CoinBaseBot PRIVATE "pch.h")

message(STATUS "CMakeLists setup complete.")
