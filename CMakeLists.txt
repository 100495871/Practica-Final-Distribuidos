cmake_minimum_required(VERSION 3.10)
project(PracticaFinal_SSDD C)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -g")

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(TIRPC libtirpc)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${TIRPC_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})

# Generación de código RPC
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/log.h
           ${CMAKE_CURRENT_BINARY_DIR}/log_clnt.c
           ${CMAKE_CURRENT_BINARY_DIR}/log_svc.c
           ${CMAKE_CURRENT_BINARY_DIR}/log_xdr.c
    COMMAND rpcgen -C -M -h -o ${CMAKE_CURRENT_BINARY_DIR}/log.h ${CMAKE_CURRENT_SOURCE_DIR}/log.x
    COMMAND rpcgen -C -M -l -o ${CMAKE_CURRENT_BINARY_DIR}/log_clnt.c ${CMAKE_CURRENT_SOURCE_DIR}/log.x
    COMMAND rpcgen -C -M -m -o ${CMAKE_CURRENT_BINARY_DIR}/log_svc.c ${CMAKE_CURRENT_SOURCE_DIR}/log.x
    COMMAND rpcgen -C -M -c -o ${CMAKE_CURRENT_BINARY_DIR}/log_xdr.c ${CMAKE_CURRENT_SOURCE_DIR}/log.x
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/log.x
)

# Silenciar warnings en código generado por rpcgen ya que no  es editable
set_source_files_properties(
    ${CMAKE_CURRENT_BINARY_DIR}/log_clnt.c
    ${CMAKE_CURRENT_BINARY_DIR}/log_svc.c
    ${CMAKE_CURRENT_BINARY_DIR}/log_xdr.c
    PROPERTIES COMPILE_FLAGS "-w -Wno-unused-variable -Wno-unused-parameter"
)

# Añadir el subdirectorio services
add_subdirectory(services)