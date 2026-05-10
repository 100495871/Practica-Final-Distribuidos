#import "@preview/hydra:0.6.2": hydra
#import "uc3m_plantilla.typ": portada_uc3m

#show: portada_uc3m.with(
  titulo: "Práctica Final: Servicio de envío de mensajes distribuido",
  asignatura: "Sistemas Distribuidos",
  titulacion: "Grado en Ingeniería Informática",
  autores: (
    [Nombre Apellido1 (NIA: 123456)],
    [Nombre Apellido2 (NIA: 789012)],
  ),
  departamento: "ARCOS",
  curso: "2025/2026",
)

#set page(
  header: context {
    hydra(1)
  },
  numbering: "1 / 1",
  footer: context {
    align(center, counter(page).display())
  }
)


= Introducción

El objetivo de este proyecto ha sido diseñar e implementar un sistema de mensajería distribuido que permite a usuarios registrados intercambiar mensajes de texto de hasta 255 caracteres, así como archivos adjuntos de cualquier tamaño. La aplicación se compone de tres elementos principales: un servidor concurrente desarrollado en C que gestiona el registro, conexión y reenvío de mensajes entre usuarios; clientes desarrollados en Python que permiten a los usuarios interactuar con el sistema; y servicios auxiliares (RPC y Web) que amplían la funcionalidad del sistema.

La primera parte de la práctica implementa el núcleo del sistema de mensajería, incluyendo el registro de usuarios, conexión/desconexión, envío de mensajes de texto y consulta de usuarios conectados. El servidor es multihilo y capaz de almacenar mensajes pendientes para usuarios desconectados, entregándolos automáticamente cuando se conectan.

La segunda parte añade tres funcionalidades: transferencia de archivos adjuntos entre usuarios (P2P, sin intervención del servidor), un servicio web de normalización de mensajes (eliminación de espacios redundantes), y un servicio RPC que registra todas las operaciones realizadas por los usuarios en el servidor.

= Diseño general

El sistema sigue una arquitectura cliente-servidor con los siguientes componentes:

*Servidor de mensajería (server.c)*: Implementado en C con sockets TCP y multihilo. Gestiona el estado de los usuarios (registrados/conectados/desconectados), almacena mensajes pendientes y actúa como intermediario en el envío de mensajes de texto. Es el único componente que conoce la ubicación de todos los clientes.

*Cliente de usuario (client.py)*: Implementado en Python con dos hilos: uno para la interfaz de usuario y envío de comandos, y otro para recibir mensajes entrantes de otros usuarios a través de un puerto de escucha. Se comunica con el servidor mediante sockets TCP y sigue el protocolo definido en el enunciado.

*Servicio RPC (rpc_server.c)*: Servidor ONC-RPC que recibe notificaciones del servidor principal cada vez que un usuario realiza una operación (REGISTER, UNREGISTER, CONNECT, DISCONNECT, USERS, SEND, SENDATTACH). Imprime por pantalla el registro de la operación.

*Servicio Web (web_service.py)*: API REST desarrollada con Flask que normaliza mensajes eliminando espacios redundantes. Es invocada por el cliente antes de enviar un mensaje.

Los clientes se comunican directamente entre sí para la transferencia de archivos (operación GETFILE), sin pasar por el servidor. Para ello, el cliente solicitante consulta al servidor la IP y puerto del usuario destino mediante el comando USERS, que ahora devuelve también la dirección de escucha de cada usuario conectado.

= Estructura de datos del servidor

El servidor mantiene una estructura global de usuarios:

```c
typedef struct {
    char        name[MAX_NAME];
    int         status;              // DISCONNECTED (0) o CONNECTED (1)
    char        ip[MAX_IP];          // IP del cliente cuando está conectado
    char        register_ip[MAX_IP]; // IP desde la que se registró
    char        port[MAX_PORT];      // Puerto de escucha del cliente
    unsigned int msg_counter;        // Contador para IDs de mensajes
    PendingMsg  pending[MAX_PENDING]; // Mensajes pendientes
    int         num_pending;
} User;
```

La lista de usuarios se protege con un mutex (`pthread_mutex_t users_mutex`) para garantizar la seguridad en un entorno multihilo. Los mensajes pendientes se almacenan en un array fijo dentro de cada usuario, con un límite de `MAX_PENDING` (100 mensajes pendientes por usuario). Cada mensaje pendiente contiene:

```c
typedef struct {
    unsigned int id;
    char sender[MAX_NAME];
    char text[MAX_MSG];
    char filename[MAX_NAME];  // Para adjuntos (Parte 2)
    int  has_attach;          // Booleano
} PendingMsg;
```

Esta estructura permite almacenar tanto mensajes de texto como mensajes con archivo adjunto, aunque el archivo en sí no se almacena en el servidor — solo el nombre del archivo, que se utiliza para notificar al destinatario qué archivo debe solicitar mediante GETFILE.

= Protocolo de comunicación

Todos los mensajes entre cliente y servidor utilizan cadenas terminadas en `\0`. Cada operación abre una nueva conexión TCP, realiza el intercambio y la cierra. A continuación se detallan los mensajes implementados:

== Registro (REGISTER)

1. Cliente → Servidor: `"REGISTER" \0 <usuario>\0`
2. Servidor → Cliente: `<byte>` (0=éxito, 1=usuario existe, 2=error)

== Baja (UNREGISTER)

1. Cliente → Servidor: `"UNREGISTER" \0 <usuario>\0`
2. Servidor → Cliente: `<byte>` (0=éxito, 1=usuario no existe, 2=error)

== Conexión (CONNECT)

1. Cliente → Servidor: `"CONNECT" \0 <usuario>\0 <puerto>\0`
2. Servidor → Cliente: `<byte>` (0=éxito, 1=usuario no existe, 2=ya conectado, 3=error)

== Desconexión (DISCONNECT)

1. Cliente → Servidor: `"DISCONNECT" \0 <usuario>\0`
2. Servidor → Cliente: `<byte>` (0=éxito, 1=no existe, 2=no conectado, 3=error)

== Envío de mensaje (SEND / SENDATTACH)

1. Cliente → Servidor: `"SEND" \0 <remitente>\0 <destino>\0 <mensaje>\0`
   o `"SENDATTACH" \0 <remitente>\0 <destino>\0 <mensaje>\0 <archivo>\0`
2. Servidor → Cliente: `<byte>` (0=éxito) + si éxito: `<id_mensaje>\0`

== Entrega servidor → cliente (SEND_MESSAGE / SEND_MESSAGE_ATTACH)

1. Servidor → Cliente: `"SEND_MESSAGE" \0 <remitente>\0 <id>\0 <mensaje>\0`
   o `"SEND_MESSAGE_ATTACH" \0 <remitente>\0 <id>\0 <mensaje>\0 <archivo>\0`

== ACK de entrega (SEND_MESS_ACK / SEND_MESS_ATTACH_ACK)

1. Servidor → Cliente: `"SEND_MESS_ACK" \0 <id>\0`
   o `"SEND_MESS_ATTACH_ACK" \0 <id>\0 <archivo>\0`

== Usuarios conectados (USERS)

1. Cliente → Servidor: `"USERS" \0 <usuario>\0`
2. Servidor → Cliente: `<byte>` (0=éxito, 1=usuario no conectado, 2=error)
3. Si éxito: `<num_usuarios>\0` + tantas cadenas como `"usuario :: IP :: puerto\0"`

= Implementación del servidor (C)

El servidor se implementa en un único archivo `server.c`. Al iniciarse, crea un socket TCP en el puerto especificado, configura `SO_REUSEADDR` y entra en un bucle de aceptación de conexiones. Por cada conexión entrante, se crea un nuevo hilo que ejecuta `handle_client()`. El hilo es detached para que el servidor no tenga que esperar a su finalización.

== Mecanismo de entrega de mensajes pendientes

Cuando un usuario se conecta exitosamente, el servidor recorre su lista de mensajes pendientes e intenta entregarlos uno a uno mediante la función `deliver_message()`. Si la entrega falla (el cliente destino no responde), el servidor lo marca como desconectado y detiene la entrega de los mensajes restantes. Solo se eliminan de la cola los mensajes entregados con éxito.

== Comunicación con el servicio RPC

El servidor obtiene la IP del servicio RPC de la variable de entorno `LOG_RPC_IP`. Para cada operación, llama a `call_rpc_log()` que, mediante `clnt_create()`, establece una conexión con el servidor RPC y envía la operación correspondiente. Para las operaciones SENDATTACH, también envía el nombre del archivo. Los errores de comunicación RPC se ignoran silenciosamente para no interrumpir el servicio principal.

== Generación de IDs de mensajes

Cada usuario mantiene un contador `msg_counter` de tipo `unsigned int`. Al enviar un mensaje, se incrementa el contador y se asigna ese valor como ID. Si el contador llega a 0 por desbordamiento, se reinicia en 1 (ya que el ID 0 no se utiliza). Esto garantiza un ciclo completo de IDs sin necesidad de persistencia.

= Implementación del cliente (Python)

El cliente se estructura en varias clases modulares:

== SocketUtils

Clase estática que proporciona métodos para enviar/recibir cadenas terminadas en `\0` y bytes individuales, así como para encontrar un puerto libre mediante `socket.bind('', 0)`.

== ServerConnection

Gestiona la comunicación con el servidor. Cada método (`register`, `unregister`, `connect`, `disconnect`, `send_message`, `send_attach`, `get_users`) abre una conexión al servidor, envía los datos según el protocolo y devuelve el resultado.

== MessageReceiver

Hilo que escucha en el puerto asignado durante la conexión. Espera conexiones entrantes del servidor (para recibir mensajes) o de otros clientes (para la transferencia de archivos). Soporta los comandos:
- `SEND_MESSAGE`: mensaje de texto simple
- `SEND_MESSAGE_ATTACH`: mensaje con archivo adjunto
- `SEND_MESS_ACK`: confirmación de entrega
- `SEND_MESS_ATTACH_ACK`: confirmación con nombre de archivo
- `GET_FILE`: solicitud de transferencia de archivo (de otros clientes)

== ChatClient

Clase principal que orquesta todas las operaciones. Mantiene un caché de usuarios conectados (`users_cache`) con su IP y puerto, actualizado mediante el comando USERS. Implementa los comandos:

- `REGISTER <user>`: registra un usuario
- `UNREGISTER <user>`: da de baja
- `CONNECT <user>`: busca puerto libre, inicia MessageReceiver, envía CONNECT al servidor
- `DISCONNECT <user>`: detiene MessageReceiver, envía DISCONNECT
- `USERS`: consulta y actualiza caché
- `SEND <user> <msg>`: envía mensaje (opcionalmente normalizado por web service)
- `SENDATTACH <user> <msg> <file>`: envía mensaje con archivo adjunto
- `GETFILE <user> <remote> <local>`: solicita archivo directamente a otro cliente
- `QUIT`: desconecta si es necesario y sale

== Normalización de mensajes (Servicio Web)

El cliente contacta con el servicio web en `http://localhost:5000/normalize` enviando un JSON con el texto. Si el servicio responde correctamente, utiliza el texto normalizado; en caso contrario, envía el mensaje original. Esto asegura robustez ante caídas del servicio web.

= Implementación del servicio RPC (C)

El servicio RPC se define en `log.x` con la siguiente interfaz:

```c
struct log_operation_args {
    string user<>;
    string operation<>;
};

struct log_sendattach_args {
    string user<>;
    string operation<>;
    string filename<>;
};

program LOGPROG {
    version LOGVERS {
        int log_operation(log_operation_args) = 1;
        int log_sendattach(log_sendattach_args) = 2;
    } = 1;
} = 0x20000001;
```

El servidor RPC (`rpc_server.c`) implementa las funciones `log_operation_1_svc()` y `log_sendattach_1_svc()`, que simplemente imprimen por pantalla la información recibida y devuelven TRUE. El programa se registra en el portmapper y entra en `svc_run()`.

El archivo `CMakeListsRoot.txt` y `services/CMakeLists.txt` automatizan la generación de los stubs RPC mediante `rpcgen` y la compilación del servidor y los clientes.

= Implementación del servicio web (Python)

El servicio web se implementa con Flask en `web_service.py`:

```python
@app.route('/normalize', methods=['GET'])
def normalize():
    data = request.get_json()
    text = data.get("text")
    normalized = " ".join(text.split())
    return jsonify({"result": normalized}), 200
```

Recibe un JSON con el campo `text`, elimina espacios redundantes usando `split()` y `join()`, y devuelve el texto normalizado.

= Compilación y ejecución

== Servidor C

Para compilar el servidor principal y el servidor RPC:

```bash
mkdir build && cd build
cmake ..
make
```

Esto genera:
- `build/server` (servidor de mensajería)
- `build/rpc_server` (servidor RPC)

== Ejecución del servidor RPC

```bash
export LOG_RPC_IP=127.0.0.1   # o IP del servidor RPC
./rpc_server
```

== Ejecución del servidor de mensajería

```bash
./server -p <puerto>
```

== Servicio web

```bash
python3 web_service.py
```

== Cliente Python

```bash
python3 client.py -s <IP_servidor> -p <puerto>
```

= Pruebas realizadas

== Prueba 1: Registro y baja de usuarios

Se registran tres usuarios: "alice", "bob", "charlie".
- Registrar "alice" → REGISTER OK
- Registrar "alice" nuevamente → USERNAME IN USE
- Registrar "bob" → REGISTER OK
- Dar de baja "alice" → UNREGISTER OK
- Verificar que "alice" ya no existe (conexión fallida) → USER DOES NOT EXIST

== Prueba 2: Conexión y desconexión

- Conectar "bob" → CONNECT OK (se muestra "CONNECTED USERS" con 1 usuario)
- Conectar "charlie" → CONNECT OK (se muestra "CONNECTED USERS" con 2 usuarios)
- Conectar "bob" nuevamente → USER ALREADY CONNECTED
- Desconectar "bob" → DISCONNECT OK
- Verificar USERS desde "charlie" → solo aparece "charlie"

== Prueba 3: Envío de mensajes (usuario conectado)

- "alice" conectado, "bob" conectado
- "alice" envía a "bob": `SEND bob Hola mundo`
- "bob" recibe: `s> MESSAGE 1 FROM alice Hola mundo END`
- "alice" recibe ACK: `c> SEND MESSAGE 1 OK`

== Prueba 4: Mensajes pendientes (usuario desconectado)

- "alice" conectado, "bob" desconectado
- "alice" envía a "bob": `SEND bob Mensaje pendiente` → SEND OK - MESSAGE 2
- En servidor: `s> MESSAGE 2 FROM alice TO bob STORED`
- "bob" se conecta → recibe automáticamente: `s> MESSAGE 2 FROM alice Mensaje pendiente END`
- "alice" recibe ACK: `c> SEND MESSAGE 2 OK`

== Prueba 5: Envío de mensajes a usuario inexistente

- "alice" envía a "nonexistent": `SEND nonexistent Hola`
- Resultado: `SEND FAIL, USER DOES NOT EXIST`

== Prueba 6: Comando USERS

- "alice" conectado, "bob" conectado, "charlie" desconectado
- "alice" ejecuta USERS
- Muestra: "CONNECTED USERS (2 users connected) OK" y lista "alice", "bob"
- También se almacena en caché IP y puerto para transferencia de archivos

== Prueba 7: Transferencia de archivos (Parte 2)

- "alice" conectado, "bob" conectado
- "alice" envía: `SENDATTACH bob Mira este archivo /tmp/test.txt`
- "bob" recibe: `s> MESSAGE 3 FROM alice Mira este archivo END FILE /tmp/test.txt`
- "bob" ejecuta: `GETFILE alice /tmp/test.txt /tmp/copy.txt`
- El archivo se transfiere correctamente P2P
- "alice" recibe ACK: `c> SENDATTACH MESSAGE 3 /tmp/test.txt OK`

== Prueba 8: Normalización de mensajes (Servicio Web)

- Servicio web corriendo en localhost:5000
- "alice" envía: `SEND bob "Hola    mundo     esto    es    una    prueba"`
- El cliente normaliza a "Hola mundo esto es una prueba" antes de enviar
- "bob" recibe el mensaje normalizado

== Prueba 9: Registro RPC de operaciones

- Servidor RPC corriendo con `LOG_RPC_IP` definida
- Ejecutar cualquier operación desde el cliente
- El servidor RPC muestra: `alice REGISTER`, `alice CONNECT`, `bob SEND`, etc.
- Para SENDATTACH: `alice SENDATTACH /tmp/test.txt`

== Prueba 10: Casos límite

- Mensaje de exactamente 255 caracteres → se envía correctamente
- Mensaje de 256 caracteres → se trunca a 255
- Archivo de 10MB para SENDATTACH → se transfiere correctamente en GETFILE
- Usuario que se desconecta abruptamente (Ctr+C) → servidor lo detecta y marca como desconectado
- Múltiples clientes concurrentes (10 conexiones simultáneas) → el servidor responde correctamente gracias al multihilo y mutex

= Conclusiones

Se ha completado el desarrollo completo del sistema de mensajería distribuido, incluyendo la funcionalidad obligatoria de la Parte 1 (registro, conexión, mensajes de texto) y las ampliaciones opcionales de la Parte 2 (transferencia de archivos P2P, servicio web de normalización, servicio RPC de registro).

El servidor en C es robusto, maneja correctamente la concurrencia mediante hilos y mutex, y cumple con todas las especificaciones del protocolo. La integración con RPC mediante `rpcgen` ha sido sencilla gracias a los ejemplos proporcionados en los ejercicios evaluables.

El cliente en Python es modular y extensible. La separación en clases (`ServerConnection`, `MessageReceiver`, `SocketUtils`) facilita el mantenimiento. La implementación de la recepción de mensajes en un hilo separado y la caché de usuarios conectados permite una experiencia de usuario fluida.

La transferencia de archivos P2P evita sobrecargar al servidor y demuestra el uso de sockets para comunicación directa entre clientes. El servicio web y el servicio RPC añaden valor al sistema, demostrando la interoperabilidad entre diferentes tecnologías (sockets, RPC, REST).

Todos los componentes se han probado en el entorno de laboratorio con múltiples contenedores Docker con direcciones IP distintas, verificando el correcto funcionamiento del sistema distribuido.

= Dificultades encontradas

1. *Gestión de mensajes pendientes*: Inicialmente se eliminaban todos los mensajes pendientes al detectar un fallo en la entrega. Se corrigió para eliminar solo los entregados exitosamente.

2. *Desbordamiento del contador de IDs*: Se implementó la lógica de reinicio a 1 cuando el `unsigned int` llega a 0 por desbordamiento, garantizando IDs únicos cíclicos.

3. *Detección de desconexión del cliente*: En la entrega de mensajes pendientes, si `deliver_message()` falla, se marca al usuario como desconectado. Esto es necesario porque el cliente podría haberse caído sin enviar DISCONNECT.

4. *IP de registro vs IP de conexión*: Para cumplir con la Sección 8.4 de la Parte 1 (solo se puede desconectar desde la IP de registro), se almacena `register_ip` y se verifica en DISCONNECT.

5. *Normalización con servicio web*: La dependencia del servicio web podría hacer fallar el envío de mensajes si el servicio no responde. Se implementó un fallback que envía el mensaje original.

Todos los materiales necesarios para reproducir la compilación y ejecución se entregan en el fichero comprimido, incluyendo código fuente, CMakeLists, Makefile y documentación.