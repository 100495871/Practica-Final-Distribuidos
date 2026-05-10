#!/bin/bash

set -e

# Configuracion
SERVER_PORT=8888
RPC_PORT=9000
WEB_PORT=5000
TEST_DIR="/tmp/ssdd_tests"
LOG_DIR="/tmp/ssdd_logs"

# Crear directorios para logs y archivos temporales
mkdir -p $TEST_DIR
mkdir -p $LOG_DIR

# Archivos de log
SERVER_LOG="$LOG_DIR/server.log"
RPC_LOG="$LOG_DIR/rpc_server.log"
WEB_LOG="$LOG_DIR/web_service.log"
CLIENT_ALICE_LOG="$LOG_DIR/client_alice.log"
CLIENT_BOB_LOG="$LOG_DIR/client_bob.log"
CLIENT_CHARLIE_LOG="$LOG_DIR/client_charlie.log"
TEST_RESULTS="$LOG_DIR/test_results.txt"

# Variables para almacenar PIDs de procesos
SERVER_PID=""
RPC_PID=""
WEB_PID=""
CLIENT_PIDS=()

# Contadores de pruebas
TESTS_PASSED=0
TESTS_FAILED=0
TOTAL_TESTS=0

# Funcion para limpiar procesos al finalizar
cleanup_processes() {
    echo "limpiando procesos..."
    
    # Enviar comando QUIT a cada cliente
    for i in "${!CLIENT_PIDS[@]}"; do
        echo "QUIT" > /tmp/client_${i}_input 2>/dev/null 
    done
    
    sleep 1
    
    # Matar clientes restantes
    for pid in "${CLIENT_PIDS[@]}"; do
        kill $pid 2>/dev/null
    done
    
    # Matar servidores
    kill $SERVER_PID 2>/dev/null
    kill $RPC_PID 2>/dev/null
    kill $WEB_PID 2>/dev/null 
    
    # Limpiar procesos rezagados
    pkill -f "python3.*client.py" 2>/dev/null 
    pkill -f "./server" 2>/dev/null 
    pkill -f "./rpc_server" 2>/dev/null 
    pkill -f "web_service.py" 2>/dev/null 
    
    sleep 1
}

# Capturar senales de terminacion
trap cleanup_processes EXIT INT TERM

# Funcion para esperar que un puerto este escuchando
wait_for_port() {
    local port=$1
    local max_attempts=30
    local attempt=0
    
    while [ $attempt -lt $max_attempts ]; do
        if nc -z localhost $port 2>/dev/null; then
            return 0
        fi
        sleep 0.5
        attempt=$((attempt + 1))
    done
    return 1
}

# Funcion para ejecutar un comando en un cliente especifico
send_to_client() {
    local client_num=$1
    local command=$2
    echo "$command" >> /tmp/client_${client_num}_input
    sleep 0.5
}

# Funcion para registrar resultado de prueba
record_test_result() {
    local test_name=$1
    local result=$2
    local description=$3
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    if [ $result -eq 0 ]; then
        echo "PASS: $test_name - $description" >> $TEST_RESULTS
        TESTS_PASSED=$((TESTS_PASSED + 1))
        echo "  [pass] $test_name"
    else
        echo "FAIL: $test_name - $description" >> $TEST_RESULTS
        TESTS_FAILED=$((TESTS_FAILED + 1))
        echo "  [fail] $test_name"
    fi
}

# Inicio de los tests
echo ""
echo "iniciando pruebas de aplicacion completa"
echo "fecha: $(date)"
echo ""

# Limpiar logs anteriores
rm -f $LOG_DIR/*.log $TEST_RESULTS

# Iniciar Servicio Web
echo "[1/4] iniciando servicio web (puerto $WEB_PORT)..."
python3 web_service.py > $WEB_LOG 2>&1 &
WEB_PID=$!
sleep 2

if wait_for_port $WEB_PORT; then
    echo "      servicio web iniciado"
else
    echo "      error: no se pudo iniciar servicio web"
    exit 1
fi

# Iniciar Servidor RPC
echo "[2/4] iniciando servidor rpc..."
./rpc_server > $RPC_LOG 2>&1 &
RPC_PID=$!
sleep 1
echo "      servidor rpc iniciado (pid: $RPC_PID)"

# Iniciar Servidor Principal
echo "[3/4] iniciando servidor principal (puerto $SERVER_PORT)..."
echo "Por favor introduce tu contraseña de root para activar el servicio de rpcbind"
sudo systemctl start rpcbind
export LOG_RPC_IP=127.0.0.1
./server -p $SERVER_PORT > $SERVER_LOG 2>&1 &
SERVER_PID=$!
sleep 2

if wait_for_port $SERVER_PORT; then
    echo "      servidor principal iniciado (pid: $SERVER_PID)"
else
    echo "      error: no se pudo iniciar servidor principal"
    exit 1
fi

# Iniciar cliente alice
echo "[4/4] iniciando clientes..."
python3 client.py -s 127.0.0.1 -p $SERVER_PORT > $CLIENT_ALICE_LOG 2>&1 &
CLIENT_PIDS[0]=$!
echo "      cliente alice iniciado (pid: ${CLIENT_PIDS[0]})"

sleep 2

# Enviar comando de prueba para verificar que el cliente responde
echo "USERS" > /tmp/client_0_input
sleep 1

if grep -q "CONNECTED USERS" $CLIENT_ALICE_LOG 2>/dev/null; then
    echo "      cliente alice responde correctamente"
else
    echo "      advertencia: cliente alice podria no estar respondiendo"
fi

echo ""
echo "ejecutando pruebas..."
echo ""

# ----------------------------------------------------------------------
# PRUEBA 1: REGISTRO DE USUARIO
# ----------------------------------------------------------------------

echo ">>> prueba 1: registro de usuario"

# Limpiar estado previo por si acaso
send_to_client 0 "UNREGISTER alice"
sleep 1

# Registrar usuario alice
send_to_client 0 "REGISTER alice"
sleep 2

# Verificar resultado en log del cliente
if grep -q "REGISTER OK" $CLIENT_ALICE_LOG; then
    record_test_result "registro de usuario" 0 "usuario alice registrado correctamente"
else
    record_test_result "registro de usuario" 1 "no se encontro 'REGISTER OK' en log"
fi

# ----------------------------------------------------------------------
# PRUEBA 2: REGISTRO DUPLICADO
# ----------------------------------------------------------------------

echo ">>> prueba 2: registro duplicado"

# Intentar registrar alice nuevamente
send_to_client 0 "REGISTER alice"
sleep 2

if grep -q "USERNAME IN USE" $CLIENT_ALICE_LOG; then
    record_test_result "registro duplicado" 0 "sistema detecto nombre de usuario en uso"
else
    record_test_result "registro duplicado" 1 "no se detecto nombre duplicado"
fi

# ----------------------------------------------------------------------
# PRUEBA 3: CONEXION AL SISTEMA
# ----------------------------------------------------------------------

echo ">>> prueba 3: conexion al sistema"

send_to_client 0 "CONNECT alice"
sleep 2

if grep -q "CONNECT OK" $CLIENT_ALICE_LOG; then
    record_test_result "conexion de usuario" 0 "usuario alice conectado correctamente"
else
    record_test_result "conexion de usuario" 1 "fallo la conexion de alice"
fi

# ----------------------------------------------------------------------
# PRUEBA 4: LISTA DE USUARIOS CONECTADOS
# ----------------------------------------------------------------------

echo ">>> prueba 4: lista de usuarios conectados"

send_to_client 0 "USERS"
sleep 2

if grep -q "CONNECTED USERS.*alice" $CLIENT_ALICE_LOG; then
    record_test_result "lista de usuarios" 0 "se muestra alice en la lista"
else
    record_test_result "lista de usuarios" 1 "alice no aparece en la lista"
fi

# ----------------------------------------------------------------------
# PRUEBA 5: REGISTRO Y CONEXION DE SEGUNDO USUARIO
# ----------------------------------------------------------------------

echo ">>> prueba 5: segundo usuario (bob)"

# Iniciar cliente bob
python3 client.py -s 127.0.0.1 -p $SERVER_PORT > $CLIENT_BOB_LOG 2>&1 &
CLIENT_PIDS[1]=$!
echo "      cliente bob iniciado (pid: ${CLIENT_PIDS[1]})"

sleep 2
send_to_client 1 "REGISTER bob"
sleep 1
send_to_client 1 "CONNECT bob"
sleep 2

if grep -q "REGISTER OK" $CLIENT_BOB_LOG && grep -q "CONNECT OK" $CLIENT_BOB_LOG; then
    record_test_result "registro y conexion bob" 0 "usuario bob registrado y conectado"
else
    record_test_result "registro y conexion bob" 1 "fallo registro o conexion de bob"
fi

# ----------------------------------------------------------------------
# PRUEBA 6: ENVIO DE MENSAJE ENTRE USUARIOS CONECTADOS
# ----------------------------------------------------------------------

echo ">>> prueba 6: envio de mensaje entre usuarios conectados"

send_to_client 0 "SEND bob Hola mundo desde alice"
sleep 3

# Verificar que alice recibio confirmacion
if grep -q "SEND OK - MESSAGE" $CLIENT_ALICE_LOG; then
    record_test_result "envio de mensaje" 0 "alice recibio confirmacion de envio"
else
    record_test_result "envio de mensaje" 1 "alice no recibio confirmacion"
fi

# Verificar que bob recibio el mensaje
if grep -q "MESSAGE.*FROM alice.*Hola mundo" $CLIENT_BOB_LOG; then
    record_test_result "recepcion de mensaje" 0 "bob recibio el mensaje de alice"
else
    record_test_result "recepcion de mensaje" 1 "bob no recibio el mensaje"
fi

# ----------------------------------------------------------------------
# PRUEBA 7: NORMALIZACION DE MENSAJES (SERVICIO WEB)
# ----------------------------------------------------------------------

echo ">>> prueba 7: normalizacion de mensajes"

# Enviar mensaje con espacios multiples
send_to_client 0 "SEND bob Hola    mundo    esto   tiene   muchos   espacios"
sleep 3

# Verificar que bob recibio el mensaje normalizado (espacios simples)
if grep -q "Hola mundo esto tiene muchos espacios" $CLIENT_BOB_LOG; then
    record_test_result "normalizacion de mensajes" 0 "mensaje normalizado correctamente"
else
    # Podria haberse enviado sin normalizar si el web service falla
    if grep -q "Hola    mundo    esto   tiene   muchos   espacios" $CLIENT_BOB_LOG; then
        record_test_result "normalizacion de mensajes" 1 "web service no disponible, mensaje sin normalizar"
    else
        record_test_result "normalizacion de mensajes" 1 "mensaje no recibido o formato incorrecto"
    fi
fi

# ----------------------------------------------------------------------
# PRUEBA 8: REGISTRO DE OPERACIONES (SERVIDOR RPC)
# ----------------------------------------------------------------------

echo ">>> prueba 8: registro de operaciones (rpc)"

# Verificar que el servidor RPC registro las operaciones
if grep -q "alice REGISTER" $RPC_LOG && \
   grep -q "alice CONNECT" $RPC_LOG && \
   grep -q "bob REGISTER" $RPC_LOG && \
   grep -q "bob CONNECT" $RPC_LOG; then
    record_test_result "registro rpc" 0 "operaciones registradas en servidor rpc"
else
    record_test_result "registro rpc" 1 "faltan operaciones en log del servidor rpc"
fi

# ----------------------------------------------------------------------
# PRUEBA 9: TERCER USUARIO Y MENSAJES MULTIPLES
# ----------------------------------------------------------------------

echo ">>> prueba 9: tercer usuario y mensajes multiples"

# Iniciar cliente charlie
python3 client.py -s 127.0.0.1 -p $SERVER_PORT > $CLIENT_CHARLIE_LOG 2>&1 &
CLIENT_PIDS[2]=$!
echo "      cliente charlie iniciado (pid: ${CLIENT_PIDS[2]})"

sleep 2
send_to_client 2 "REGISTER charlie"
sleep 1
send_to_client 2 "CONNECT charlie"
sleep 2

# alice envia multiples mensajes a charlie
for i in 1 2 3; do
    send_to_client 0 "SEND charlie Mensaje numero $i para charlie"
    sleep 1
done

sleep 2

# Verificar que charlie recibio todos los mensajes
MSG_COUNT=$(grep -c "MESSAGE.*FROM alice" $CLIENT_CHARLIE_LOG || echo "0")
if [ "$MSG_COUNT" -ge 3 ]; then
    record_test_result "mensajes multiples" 0 "charlie recibio $MSG_COUNT mensajes"
else
    record_test_result "mensajes multiples" 1 "charlie recibio solo $MSG_COUNT de 3 mensajes"
fi

# ----------------------------------------------------------------------
# PRUEBA 10: DESCONEXION Y RECONEXION
# ----------------------------------------------------------------------

echo ">>> prueba 10: desconexion y reconexion"

send_to_client 1 "DISCONNECT bob"
sleep 2

if grep -q "DISCONNECT OK" $CLIENT_BOB_LOG; then
    record_test_result "desconexion" 0 "bob se desconecto correctamente"
else
    record_test_result "desconexion" 1 "fallo desconexion de bob"
fi

# Reconectar bob
send_to_client 1 "CONNECT bob"
sleep 2

if grep -q "CONNECT OK" $CLIENT_BOB_LOG; then
    record_test_result "reconexion" 0 "bob se reconecto correctamente"
else
    record_test_result "reconexion" 1 "fallo reconexion de bob"
fi

# ----------------------------------------------------------------------
# PRUEBA 11: MENSAJE A USUARIO DESCONECTADO
# ----------------------------------------------------------------------

echo ">>> prueba 11: mensaje a usuario desconectado"

# Desconectar bob nuevamente
send_to_client 1 "DISCONNECT bob"
sleep 2

# alice envia mensaje a bob (desconectado)
send_to_client 0 "SEND bob Mensaje para bob mientras estaba desconectado"
sleep 2

# Verificar que alice recibio OK (el mensaje se almacena)
if grep -q "SEND OK - MESSAGE" $CLIENT_ALICE_LOG; then
    record_test_result "mensaje a desconectado" 0 "mensaje almacenado correctamente"
else
    record_test_result "mensaje a desconectado" 1 "fallo envio a usuario desconectado"
fi

# Reconectar bob y verificar que recibe mensaje pendiente
send_to_client 1 "CONNECT bob"
sleep 3

if grep -q "MESSAGE.*FROM alice.*Mensaje para bob mientras estaba desconectado" $CLIENT_BOB_LOG; then
    record_test_result "mensaje pendiente" 0 "bob recibio mensaje pendiente al reconectar"
else
    record_test_result "mensaje pendiente" 1 "mensaje pendiente no fue entregado"
fi

# ----------------------------------------------------------------------
# PRUEBA 12: BAJA DE USUARIO
# ----------------------------------------------------------------------

echo ">>> prueba 12: baja de usuario"

send_to_client 2 "UNREGISTER charlie"
sleep 2

if grep -q "UNREGISTER OK" $CLIENT_CHARLIE_LOG; then
    record_test_result "baja de usuario" 0 "usuario charlie eliminado correctamente"
else
    record_test_result "baja de usuario" 1 "fallo baja de usuario"
fi

# Intentar enviar mensaje a usuario dado de baja
send_to_client 0 "SEND charlie Mensaje a usuario eliminado"
sleep 2

if grep -q "SEND FAIL, USER DOES NOT EXIST" $CLIENT_ALICE_LOG; then
    record_test_result "mensaje a usuario eliminado" 0 "sistema detecto usuario inexistente"
else
    record_test_result "mensaje a usuario eliminado" 1 "no se detecto que el usuario no existe"
fi

# ----------------------------------------------------------------------
# PRUEBA 13: VERIFICACION DE LOGS DEL SERVIDOR
# ----------------------------------------------------------------------

echo ">>> prueba 13: verificacion de logs del servidor"

# Verificar que el servidor principal registro las operaciones
if grep -q "s> REGISTER alice OK" $SERVER_LOG && \
   grep -q "s> CONNECT alice OK" $SERVER_LOG && \
   grep -q "s> SEND MESSAGE" $SERVER_LOG; then
    record_test_result "logs del servidor" 0 "servidor principal registro operaciones"
else
    record_test_result "logs del servidor" 1 "faltan entradas en log del servidor"
fi

# ----------------------------------------------------------------------
# PRUEBA 14: CIERRE DE CLIENTES
# ----------------------------------------------------------------------

echo ">>> prueba 14: cierre de clientes"

for i in 0 1 2; do
    send_to_client $i "QUIT"
done
sleep 2

# Verificar que los clientes terminaron
CLIENTS_RUNNING=0
for pid in "${CLIENT_PIDS[@]}"; do
    if kill -0 $pid 2>/dev/null; then
        CLIENTS_RUNNING=$((CLIENTS_RUNNING + 1))
    fi
done

if [ $CLIENTS_RUNNING -eq 0 ]; then
    record_test_result "cierre de clientes" 0 "todos los clientes terminaron correctamente"
else
    record_test_result "cierre de clientes" 1 "$CLIENTS_RUNNING clientes no terminaron"
fi

# ----------------------------------------------------------------------
# RESUMEN FINAL
# ----------------------------------------------------------------------

echo ""
echo "resultados de las pruebas"
echo "-----------------------"
echo "pruebas superadas: $TESTS_PASSED"
echo "pruebas falladas:   $TESTS_FAILED"
echo "total:             $TOTAL_TESTS"
echo ""

if [ $TESTS_FAILED -eq 0 ]; then
    echo "todas las pruebas pasaron correctamente"
    echo "la aplicacion funciona segun lo especificado"
    EXIT_CODE=0
else
    echo "algunas pruebas fallaron. revisar logs en $LOG_DIR"
    echo "  - log del servidor: $SERVER_LOG"
    echo "  - log del servidor rpc: $RPC_LOG"
    echo "  - log del servicio web: $WEB_LOG"
    echo "  - logs de clientes: $LOG_DIR/client_*.log"
    EXIT_CODE=1
fi

echo ""
echo "detalle de resultados guardado en: $TEST_RESULTS"
echo ""

exit $EXIT_CODE