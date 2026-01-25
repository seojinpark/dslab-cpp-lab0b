cmake_minimum_required(VERSION 3.11 FATAL_ERROR)
project(Accumulator)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TORCH_CXX_FLAGS} -std=c++20 -Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -g")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Option for enabling OTEL tracing
option(TRACING "Enable OTEL tracing." OFF)

if (TRACING)
    message(STATUS "Building with OTEL tracing enabled.")
    add_definitions(-DTRACING)
else()
    message(STATUS "Building without OTEL tracing.")
endif()

# This depends on how the setup script install the grpc
set(grpc_prefix "$ENV{HOME}/.local")

set(CMAKE_PREFIX_PATH ${grpc_prefix} CACHE PATH "libs prefix path")

# Add spdlog library
add_subdirectory(libs/spdlog)

# includes grpc
include(cmake/common.cmake)

if (TRACING)
  # Find OpenTelemetry packages for tracing
  find_package(opentelemetry-cpp CONFIG REQUIRED)
  message(STATUS "Using opentelemetry-cpp ${opentelemetry-cpp_VERSION}")
  
  add_subdirectory(cmake/wrappers/otel_spdlog)
endif()

# Proto file
get_filename_component(dp_proto "protos/accumulator.proto" ABSOLUTE)
get_filename_component(dp_proto_path "${dp_proto}" PATH)

# Generated sources
set(dp_proto_srcs "${CMAKE_CURRENT_BINARY_DIR}/accumulator.pb.cc")
set(dp_proto_hdrs "${CMAKE_CURRENT_BINARY_DIR}/accumulator.pb.h")
set(dp_grpc_srcs "${CMAKE_CURRENT_BINARY_DIR}/accumulator.grpc.pb.cc")
set(dp_grpc_hdrs "${CMAKE_CURRENT_BINARY_DIR}/accumulator.grpc.pb.h")
add_custom_command(
      OUTPUT "${dp_proto_srcs}" "${dp_proto_hdrs}" "${dp_grpc_srcs}" "${dp_grpc_hdrs}"
      COMMAND ${_PROTOBUF_PROTOC}
      ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
        --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
        -I "${dp_proto_path}"
        --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
        "${dp_proto}"
      DEPENDS "${dp_proto}")

# Include generated *.pb.h files
include_directories("${CMAKE_CURRENT_BINARY_DIR}")

# Include DDB headers
include_directories("${CMAKE_PREFIX_PATH}/include")

# Include our custom headers (logger, tracing)
include_directories("${CMAKE_SOURCE_DIR}/inc")

# dp_grpc_proto
add_library(dp_grpc_proto
  ${dp_grpc_srcs}
  ${dp_grpc_hdrs}
  ${dp_proto_srcs}
  ${dp_proto_hdrs})
target_link_libraries(dp_grpc_proto
  ${_REFLECTION}
  ${_GRPC_GRPCPP}
  ${_PROTOBUF_LIBPROTOBUF}
  paho-mqtt3c
)

# Common libraries for both client and server
set(COMMON_LIBS
  dp_grpc_proto
  ${_REFLECTION}
  ${_GRPC_GRPCPP}
  ${_PROTOBUF_LIBPROTOBUF}
  ${TORCH_LIBRARIES}
  spdlog::spdlog
)

# Add OTEL libraries if tracing is enabled
if (TRACING)
  list(APPEND COMMON_LIBS
    opentelemetry-cpp::trace
    opentelemetry-cpp::otlp_grpc_exporter
    opentelemetry-cpp::otlp_grpc_log_record_exporter
    opentelemetry-cpp::logs
    opentelemetry_spdlog_sink
  )
endif()

# Linking info for runtime.
add_executable(wcClient
  "src/clientMain.cpp" "src/rpcClient.cpp" "src/rpcTracker.cpp")
target_link_libraries(wcClient ${COMMON_LIBS})

add_executable(wcServer
  "src/serverMain.cpp" "src/rpcService.cpp" "src/unackedRpcResults.cpp")
target_link_libraries(wcServer ${COMMON_LIBS})

set_property(TARGET wcClient wcServer PROPERTY CXX_STANDARD 20)
