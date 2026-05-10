cmake_minimum_required(VERSION 3.10)
project(PracticaFinal_SSDD C)
set_directory_properties(PROPERTIES 
    ADDITIONAL_CLEAN_FILES "log_clnt.c;log_svc.c;log_xdr.c;log.h"
)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -g")

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(TIRPC libtirpc)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${TIRPC_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})

# Definir variables de ruta absolutas para evitar confusiones
set(RPC_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(RPC_SPEC    ${CMAKE_CURRENT_SOURCE_DIR}/log.x)

set(RPC_OUTPUTS
    ${RPC_GEN_DIR}/log.h
    ${RPC_GEN_DIR}/log_clnt.c
    ${RPC_GEN_DIR}/log_svc.c
    ${RPC_GEN_DIR}/log_xdr.c
)

# El comando de generación (SIN rm)
add_custom_command(
    OUTPUT ${RPC_OUTPUTS}
    COMMAND rpcgen -C -M -h -o ${RPC_GEN_DIR}/log.h ${RPC_SPEC}
    COMMAND rpcgen -C -M -l -o ${RPC_GEN_DIR}/log_clnt.c ${RPC_SPEC}
    COMMAND rpcgen -C -M -m -o ${RPC_GEN_DIR}/log_svc.c ${RPC_SPEC}
    COMMAND rpcgen -C -M -c -o ${RPC_GEN_DIR}/log_xdr.c ${RPC_SPEC}
    DEPENDS ${RPC_SPEC}
    COMMENT "Generando archivos RPC desde log.x"
    VERBATIM
)

# Marcar los archivos como GENERATED para que CMake no se queje de que no existen al inicio
set_source_files_properties(${RPC_OUTPUTS} PROPERTIES GENERATED TRUE)


# Añadir el subdirectorio services
add_subdirectory(services)