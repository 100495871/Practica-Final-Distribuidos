# services/CMakeLists.txt
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build)
# Servidor de Mensajería (usa el cliente RPC)
add_executable(server server.c
    ${CMAKE_CURRENT_BINARY_DIR}/../log_clnt.c
    ${CMAKE_CURRENT_BINARY_DIR}/../log_xdr.c
)
target_link_libraries(server Threads::Threads ${TIRPC_LIBRARIES})
target_include_directories(server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Servidor RPC de registro
add_executable(rpc_server rpc_server.c
    ${CMAKE_CURRENT_BINARY_DIR}/../log_svc.c
    ${CMAKE_CURRENT_BINARY_DIR}/../log_xdr.c
)
set(RPCGEN_FILES
    ${CMAKE_CURRENT_BINARY_DIR}/../log_clnt.c
    ${CMAKE_CURRENT_BINARY_DIR}/../log_xdr.c
    ${CMAKE_CURRENT_BINARY_DIR}/../log_svc.c
)

# Aplicar el flag -w (silenciar todo) solo a esos archivos específicos
set_source_files_properties(${RPCGEN_FILES} PROPERTIES COMPILE_FLAGS "-w")

# El resto de opciones generales para el ejecutable
target_compile_options(server PRIVATE -Wall -Wextra -g)
target_compile_options(rpc_server PRIVATE -Wall -Wextra -g)

target_link_libraries(rpc_server ${TIRPC_LIBRARIES})
target_include_directories(rpc_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)