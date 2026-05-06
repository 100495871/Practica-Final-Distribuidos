cmake_minimum_required(VERSION 3.10)
project(PracticaFinal_SSDD C)

# Configuración de compilación
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -g")

# Buscar dependencias
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(TIRPC libtirpc)

# Incluir directorios
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${TIRPC_INCLUDE_DIRS})

# Servidor de Mensajería (C)
add_executable(server server.c)
target_link_libraries(server Threads::Threads ${TIRPC_LIBRARIES})

# Nota: Para la parte de RPC, aquí añadirías las reglas de rpcgen