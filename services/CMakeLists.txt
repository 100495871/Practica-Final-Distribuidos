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
target_link_libraries(rpc_server ${TIRPC_LIBRARIES})
target_include_directories(rpc_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)