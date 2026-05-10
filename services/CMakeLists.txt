# services/CMakeLists.txt

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build)

# Definición de rutas (Todo dentro del BINARY_DIR de este subdirectorio)
set(RPC_SPEC ${CMAKE_CURRENT_SOURCE_DIR}/../log.x)
set(RPC_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR})

set(RPC_OUTPUTS
    ${RPC_GEN_DIR}/log.h
    ${RPC_GEN_DIR}/log_clnt.c
    ${RPC_GEN_DIR}/log_svc.c
    ${RPC_GEN_DIR}/log_xdr.c
)
# El comando rpcgen (La "receta")
add_custom_command(
    OUTPUT ${RPC_OUTPUTS}
    COMMAND rpcgen -C -M -h -o log.h log.x
    COMMAND rpcgen -C -M -l -o log_clnt.c log.x
    COMMAND rpcgen -C -M -m -o log_svc.c log.x
    COMMAND rpcgen -C -M -c -o log_xdr.c log.x
    DEPENDS log.x
    COMMENT "Generando stubs de RPC en ${RPC_GEN_DIR}..."
    VERBATIM
)

# Marcar como generados y silenciar warnings
set_source_files_properties(${RPC_OUTPUTS} PROPERTIES GENERATED TRUE)
set_source_files_properties(${RPC_OUTPUTS} PROPERTIES COMPILE_FLAGS "-w")

# Servidor de Mensajería (Cliente RPC)
# Usamos las rutas exactas de RPC_OUTPUTS para que CMake sepa que debe ejecutarlos
add_executable(server server.c
    ${RPC_GEN_DIR}/log_clnt.c
    ${RPC_GEN_DIR}/log_xdr.c
)
target_link_libraries(server Threads::Threads ${TIRPC_LIBRARIES})
# Importante: Incluir RPC_GEN_DIR para que encuentre log.h
target_include_directories(server PRIVATE ${RPC_GEN_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_options(server PRIVATE -Wall -Wextra -g)

# Servidor RPC de registro
add_executable(rpc_server rpc_server.c
    ${RPC_GEN_DIR}/log_svc.c
    ${RPC_GEN_DIR}/log_xdr.c
)
target_link_libraries(rpc_server ${TIRPC_LIBRARIES})
target_include_directories(rpc_server PRIVATE ${RPC_GEN_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_options(rpc_server PRIVATE -Wall -Wextra -g)