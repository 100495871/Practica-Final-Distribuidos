#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <netdb.h>
#include <limits.h>
#include <rpc/rpc.h>
#include "log.h"

/* CONSTANTES */
#define MAX_USERS        100
#define MAX_PENDING      100
#define MAX_NAME         256
#define MAX_MSG          256
#define MAX_PORT         16
#define MAX_IP           64
#define BACKLOG          10

#define DISCONNECTED     0
#define CONNECTED        1

/* ESTRUCTURAS */

typedef struct {
    unsigned int id;
    char sender[MAX_NAME];
    char text[MAX_MSG];
    char filename[MAX_NAME]; /* Parte 2 */
    int  has_attach;         /* boolean */
} PendingMsg;

typedef struct {
    char        name[MAX_NAME];
    int         status;
    char        ip[MAX_IP];
    char        register_ip[MAX_IP]; /* Parte 1, Secc 8.4 */
    char        port[MAX_PORT];
    unsigned int msg_counter;
    PendingMsg  pending[MAX_PENDING];
    int         num_pending;
} User;

typedef struct {
    int  fd;
    char ip[MAX_IP];
} ClientArgs;

/* VARIABLES GLOBALES */

static User           users[MAX_USERS];
static int            num_users = 0;
static pthread_mutex_t users_mutex = PTHREAD_MUTEX_INITIALIZER;
static int            server_fd = -1;
static char           *rpc_server_ip = NULL;

/* Cliente RPC */

static void call_rpc_log(const char *user, const char *op, const char *filename) {
    // Verificar si hay una dirección IP del servidor RPC configurada
    if (!rpc_server_ip) return;
    
    // Manejador del cliente RPC
    CLIENT *clnt;
    // Código de estado de la llamada RPC
    enum clnt_stat status;
    // Variable para almacenar el resultado de la operación (no se usa)
    int result;
    
    // Crear el cliente RPC para conectarse al servidor usando protocolo TCP
    clnt = clnt_create(rpc_server_ip, LOGPROG, LOGVERS, "tcp");
    // Si falla la creación del cliente, salir silenciosamente
    if (clnt == NULL) {
        return;
    }
    
    // Determinar qué tipo de operación RPC ejecutar según si hay nombre de archivo o no
    if (filename == NULL) {
        // Operación de log simple (sin archivo adjunto)
        struct log_operation_args args;
        args.user = (char *)user;
        args.operation = (char *)op;
        status = log_operation_1(&args, &result, clnt);
    } else {
        // Operación de log con archivo adjunto
        struct log_sendattach_args args;
        args.user = (char *)user;
        args.operation = (char *)op;
        args.filename = (char *)filename;
        status = log_sendattach_1(&args, &result, clnt);
    }
    
    // Verificar si la comunicación RPC fue exitosa
    if (status != RPC_SUCCESS) {
        // Error en la comunicación RPC (no se hace nada, solo comentario)
    }
    
    // Destruir el cliente RPC y liberar recursos
    clnt_destroy(clnt);
}

/* FUNCIONES AUXILIARES DE RED */

static int send_string(int fd, const char *s) {
    int   len = (int)strlen(s) + 1;
    // Inicializar el contador de bytes enviados
    int   sent = 0;
    // Bucle mientras no se haya enviado la cadena completa (incluyendo el '\0')
    while (sent < len) {
        // Enviar los bytes pendientes mediante la llamada send()
        int r = (int)send(fd, s + sent, len - sent, 0);
        // Si no se pudo enviar ningún byte o hubo error, retornar -1
        if (r <= 0) {
            return -1;
        }
        // Acumular la cantidad de bytes enviados en esta iteración
        sent += r;
    }
    // Retornar 0 indicando que la cadena se envió correctamente
    return 0;
}

static int recv_string(int fd, char *buf, int maxlen) {
    // Inicializar el índice del buffer donde se irán guardando los caracteres
    int  i = 0;
    // Variable para almacenar temporalmente cada carácter recibido
    char c;
    // Bucle mientras haya espacio en el buffer (dejando un lugar para el terminador nulo)
    while (i < maxlen - 1) {
        // Recibir un solo carácter del socket
        int r = (int)recv(fd, &c, 1, 0);
        // Si no se recibió ningún byte o hubo error, retornar -1
        if (r <= 0) return -1;
        // Si se recibió el carácter nulo, salir del bucle (fin de la cadena)
        if (c == '\0') break;
        // Almacenar el carácter recibido en el buffer y avanzar el índice
        buf[i++] = c;
    }
    // Agregar el terminador nulo al final de la cadena
    buf[i] = '\0';
    // Retornar la longitud de la cadena recibida (sin contar el nulo)
    return i;
}

static int send_byte(int fd, unsigned char b) {
    // Enviar un solo byte por el socket y verificar si se envió correctamente
    // Si se envió exactamente 1 byte retornar 0, en caso contrario retornar -1
    return (send(fd, &b, 1, 0) == 1) ? 0 : -1;
}

static void get_local_ip(char *ip_buf, int len) {
    // Buffer para almacenar el nombre del host local
    char hostname[256];
    // Obtener el nombre del host local
    if (gethostname(hostname, sizeof(hostname)) < 0) {
        // Si falla, usar la dirección de loopback por defecto
        strncpy(ip_buf, "127.0.0.1", len - 1);
        return;
    }
    // Estructuras para la resolución de direcciones
    struct addrinfo hints, *res;
    // Inicializar la estructura hints con ceros
    memset(&hints, 0, sizeof(hints));
    // Especificar que se quiere una dirección IPv4
    hints.ai_family   = AF_INET;
    // Especificar el tipo de socket (TCP)
    hints.ai_socktype = SOCK_STREAM;
    // Resolver el nombre del host a una dirección IP
    if (getaddrinfo(hostname, NULL, &hints, &res) == 0 && res) {
        // Obtener la estructura de dirección IPv4 desde el resultado
        struct sockaddr_in *sin = (struct sockaddr_in *)res->ai_addr;
        // Convertir la dirección IP de formato binario a texto
        inet_ntop(AF_INET, &sin->sin_addr, ip_buf, len);
        // Liberar la memoria asignada por getaddrinfo
        freeaddrinfo(res);
    } else {
        // Si falla la resolución, usar la dirección de loopback por defecto
        strncpy(ip_buf, "127.0.0.1", len - 1);
    }
}

static int find_user(const char *name) {
    // Iterar sobre el arreglo de usuarios registrados
    for (int i = 0; i < num_users; i++) {
        // Comparar el nombre buscado con el nombre del usuario actual
        if (strcmp(users[i].name, name) == 0) return i;
    }
    // Retornar -1 si no se encontró el usuario
    return -1;
}

/* ENVÍO SERVIDOR → CLIENTE */

static int deliver_message(const char *ip, const char *port, const char *sender, 
                          unsigned int id, const char *message, const char *filename) {
    // Crear un socket TCP para conectarse al cliente
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    // Si falla la creación del socket, retornar -1
    if (sock < 0) return -1;

    // Estructura para almacenar la dirección del cliente
    struct sockaddr_in addr;
    // Inicializar toda la estructura con ceros
    memset(&addr, 0, sizeof(addr));
    // Especificar la familia de direcciones IPv4
    addr.sin_family = AF_INET;
    // Convertir el número de puerto de texto a entero y luego a formato de red
    addr.sin_port   = htons((uint16_t)atoi(port));
    // Convertir la dirección IP de texto a formato binario
    inet_pton(AF_INET, ip, &addr.sin_addr);

    // Conectarse al cliente
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        // Si falla la conexión, cerrar el socket y retornar -1
        close(sock);
        return -1;
    }

    // Buffer para convertir el ID del mensaje a cadena de texto
    char id_str[32];
    // Convertir el ID numérico a cadena
    sprintf(id_str, "%u", id);

    // Determinar el tipo de mensaje según si hay archivo adjunto o no
    if (filename == NULL) {
        // Mensaje sin archivo adjunto
        send_string(sock, "SEND_MESSAGE");
        send_string(sock, sender);
        send_string(sock, id_str);
        send_string(sock, message);
    } else {
        //Mensaje con archivo adjunto
        send_string(sock, "SEND_MESSAGE_ATTACH");
        send_string(sock, sender);
        send_string(sock, id_str);
        send_string(sock, message);
        send_string(sock, filename);
    }
    // Cerrar el socket después de enviar todos los datos
    close(sock);
    // Retornar 0 indicando éxito
    return 0;
}

static void send_ack_to_sender(const char *ip, const char *port, unsigned int id, const char *filename) {
    // Crear socket TCP para enviar la confirmación
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return;

    // Configurar dirección del cliente que recibió el mensaje original
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)atoi(port));
    inet_pton(AF_INET, ip, &addr.sin_addr);

    // Conectarse al cliente para entregar el ACK
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return;
    }

    // Convertir ID numérico a cadena para enviarlo por el socket
    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%u", id);

    // Distinguir si el mensaje original tenía archivo adjunto
    if (filename == NULL) {
        // ACK para mensaje simple
        send_string(sock, "SEND_MESS_ACK");
        send_string(sock, id_str);
    } else {
        // ACK para mensaje con archivo adjunto (se incluye el nombre del archivo)
        send_string(sock, "SEND_MESS_ATTACH_ACK");
        send_string(sock, id_str);
        send_string(sock, filename);
    }
    close(sock);
}

/* MANEJADORES DE OPERACIONES */

static void handle_register(int fd, const char *ip) {
    char name[MAX_NAME];
    // Recibir el nombre del usuario que se quiere registrar
    recv_string(fd, name, MAX_NAME);
    // Registrar la operación en el log RPC
    call_rpc_log(name, "REGISTER", NULL);
    
    // Bloquear el mutex para acceder a la lista de usuarios de forma segura
    pthread_mutex_lock(&users_mutex);
    // Verificar si el usuario ya existe o si se alcanzó el límite máximo
    if (find_user(name) >= 0 || num_users >= MAX_USERS) {
        pthread_mutex_unlock(&users_mutex);
        // Enviar código de error: 1=ya existe, 2=servidor lleno
        send_byte(fd, find_user(name) >= 0 ? 1 : 2);
        printf("s> REGISTER %s FAIL\n", name);
    } else {
        // Registrar nuevo usuario en el primer slot disponible
        User *u = &users[num_users++];
        memset(u, 0, sizeof(User));
        strncpy(u->name, name, MAX_NAME - 1);
        strncpy(u->register_ip, ip, MAX_IP - 1);
        // Estado inicial: desconectado hasta que haga CONNECT
        u->status = DISCONNECTED;
        pthread_mutex_unlock(&users_mutex);
        // Enviar código de éxito
        send_byte(fd, 0);
        printf("s> REGISTER %s OK\n", name);
    }
}

static void handle_unregister(int fd) {
    char name[MAX_NAME];
    // Recibir el nombre del usuario que se quiere dar de baja
    recv_string(fd, name, MAX_NAME);
    call_rpc_log(name, "UNREGISTER", NULL);
    
    pthread_mutex_lock(&users_mutex);
    int idx = find_user(name);
    // Si el usuario no existe, enviar error
    if (idx < 0) {
        pthread_mutex_unlock(&users_mutex);
        send_byte(fd, 1);
        printf("s> UNREGISTER %s FAIL\n", name);
    } else {
        // Eliminar el usuario desplazando los siguientes hacia arriba
        for (int i = idx; i < num_users - 1; i++) users[i] = users[i+1];
        num_users--;
        pthread_mutex_unlock(&users_mutex);
        send_byte(fd, 0);
        printf("s> UNREGISTER %s OK\n", name);
    }
}

static void handle_connect(int fd, const char *ip) {
    char name[MAX_NAME], port[MAX_PORT];
    // Recibir nombre del usuario y puerto donde escucha para mensajes
    recv_string(fd, name, MAX_NAME);
    recv_string(fd, port, MAX_PORT);
    call_rpc_log(name, "CONNECT", NULL);
    
    pthread_mutex_lock(&users_mutex);
    int idx = find_user(name);
    // Validar que el usuario exista
    if (idx < 0) {
        pthread_mutex_unlock(&users_mutex);
        send_byte(fd, 1);
        printf("s> CONNECT %s FAIL\n", name);
    // Validar que no esté ya conectado
    } else if (users[idx].status == CONNECTED) {
        pthread_mutex_unlock(&users_mutex);
        send_byte(fd, 2);
        printf("s> CONNECT %s FAIL\n", name);
    } else {
        // Marcar como conectado y guardar IP y puerto para comunicaciones futuras
        users[idx].status = CONNECTED;
        strncpy(users[idx].ip, ip, MAX_IP-1);
        strncpy(users[idx].port, port, MAX_PORT-1);
        
        // Entregar todos los mensajes pendientes que tenía almacenados
        int ok_delivered[MAX_PENDING];
        for (int i = 0; i < users[idx].num_pending; i++) {
            // Intentar entregar el mensaje al cliente recién conectado
            if (deliver_message(ip, port, users[idx].pending[i].sender, users[idx].pending[i].id, 
                                 users[idx].pending[i].text, users[idx].pending[i].has_attach ? users[idx].pending[i].filename : NULL) == 0) {
                ok_delivered[i] = 1;
                printf("s> SEND MESSAGE %u FROM %s TO %s\n", users[idx].pending[i].id, users[idx].pending[i].sender, name);
                
                // Notificar al remitente original que su mensaje fue entregado
                int sidx = find_user(users[idx].pending[i].sender);
                if (sidx >= 0 && users[sidx].status == CONNECTED) {
                    send_ack_to_sender(users[sidx].ip, users[sidx].port, users[idx].pending[i].id, users[idx].pending[i].has_attach ? users[idx].pending[i].filename : NULL);
                }
            } else {
                ok_delivered[i] = 0;
                // Si falló la entrega, marcar como desconectado
                users[idx].status = DISCONNECTED;
                break;
            }
        }
        
        // Eliminar solo los mensajes que se entregaron exitosamente
        int j = 0;
        for (int i = 0; i < users[idx].num_pending; i++) {
            if (!ok_delivered[i]) {
                users[idx].pending[j++] = users[idx].pending[i];
            }
        }
        users[idx].num_pending = j;
        
        pthread_mutex_unlock(&users_mutex);
        // Enviar confirmación de conexión exitosa
        send_byte(fd, 0);
        printf("s> CONNECT %s OK\n", name);
    }
}

static void handle_disconnect(int fd, const char *ip) {
    char name[MAX_NAME];
    recv_string(fd, name, MAX_NAME);
    call_rpc_log(name, "DISCONNECT", NULL);
    
    pthread_mutex_lock(&users_mutex);
    int idx = find_user(name);
    // Verificar si el usuario existe y está conectado
    if (idx < 0 || users[idx].status == DISCONNECTED) {
        pthread_mutex_unlock(&users_mutex);
        send_byte(fd, idx < 0 ? 1 : 2);
        printf("s> DISCONNECT %s FAIL\n", name);
        return;
    }
    
    // Seguridad: solo permite desconectarse desde la IP de registro
    if (strcmp(users[idx].register_ip, ip) != 0) {
        pthread_mutex_unlock(&users_mutex);
        send_byte(fd, 3);
        printf("s> DISCONNECT %s FAIL (IP mismatch)\n", name);
        return;
    }

    // Cambiar estado a desconectado sin eliminar la cuenta
    users[idx].status = DISCONNECTED;
    pthread_mutex_unlock(&users_mutex);
    send_byte(fd, 0);
    printf("s> DISCONNECT %s OK\n", name);
}

static void handle_send_generic(int fd, int has_attach) {
    char sender[MAX_NAME], receiver[MAX_NAME], message[MAX_MSG], filename[MAX_NAME];
    // Recibir todos los datos del mensaje según tenga adjunto o no
    recv_string(fd, sender, MAX_NAME);
    recv_string(fd, receiver, MAX_NAME);
    recv_string(fd, message, MAX_MSG);
    if (has_attach) {
        recv_string(fd, filename, MAX_NAME);
        call_rpc_log(sender, "SENDATTACH", filename);
    } else {
        call_rpc_log(sender, "SEND", NULL);
    }
    
    pthread_mutex_lock(&users_mutex);
    int sidx = find_user(sender);
    int ridx = find_user(receiver);
    
    // Validar existencia de remitente y destinatario
    if (ridx < 0) { pthread_mutex_unlock(&users_mutex); send_byte(fd, 1); return; }
    if (sidx < 0) { pthread_mutex_unlock(&users_mutex); send_byte(fd, 2); return; }
    
    // Generar ID único para este mensaje del remitente
    unsigned int msg_id = ++users[sidx].msg_counter;
    if (msg_id == 0) msg_id = users[sidx].msg_counter = 1;

    // Guardar datos del destinatario si está conectado, para entrega inmediata
    int r_conn = (users[ridx].status == CONNECTED);
    char rip[MAX_IP], rport[MAX_PORT];
    if (r_conn) { strcpy(rip, users[ridx].ip); strcpy(rport, users[ridx].port); }
    // Si no está conectado, almacenar en pendientes si hay espacio
    else if (users[ridx].num_pending < MAX_PENDING) {
        PendingMsg *pm = &users[ridx].pending[users[ridx].num_pending++];
        pm->id = msg_id; pm->has_attach = has_attach;
        strcpy(pm->sender, sender); strcpy(pm->text, message);
        if (has_attach) strcpy(pm->filename, filename);
    }
    
    // Guardar IP del remitente para enviarle ACK después
    int s_conn = (users[sidx].status == CONNECTED);
    char sip[MAX_IP], sport[MAX_PORT];
    if (s_conn) { strcpy(sip, users[sidx].ip); strcpy(sport, users[sidx].port); }
    pthread_mutex_unlock(&users_mutex);
    
    // Enviar confirmación al cliente con el ID asignado
    char id_str[32]; snprintf(id_str, sizeof(id_str), "%u", msg_id);
    send_byte(fd, 0);
    send_string(fd, id_str);
    
    // Entregar mensaje si el destinatario está conectado
    if (r_conn) {
        if (deliver_message(rip, rport, sender, msg_id, message, has_attach ? filename : NULL) == 0) {
            // Entrega exitosa: notificar al remitente con ACK
            if (s_conn) send_ack_to_sender(sip, sport, msg_id, has_attach ? filename : NULL);
        } else {
            // Falló la entrega: marcar destinatario como desconectado y guardar en pendientes
            pthread_mutex_lock(&users_mutex);
            int r = find_user(receiver);
            if (r >= 0) {
                users[r].status = DISCONNECTED;
                if (users[r].num_pending < MAX_PENDING) {
                    PendingMsg *pm = &users[r].pending[users[r].num_pending++];
                    pm->id = msg_id; pm->has_attach = has_attach;
                    strcpy(pm->sender, sender); strcpy(pm->text, message);
                    if (has_attach) strcpy(pm->filename, filename);
                }
            }
            pthread_mutex_unlock(&users_mutex);
        }
    }
}

static void handle_users(int fd) {
    char name[MAX_NAME];
    recv_string(fd, name, MAX_NAME);
    call_rpc_log(name, "USERS", NULL);
    
    pthread_mutex_lock(&users_mutex);
    int idx = find_user(name);
    // Solo usuarios registrados y conectados pueden consultar la lista
    if (idx < 0 || users[idx].status == DISCONNECTED) {
        pthread_mutex_unlock(&users_mutex); send_byte(fd, 1);
    } else {
        // Contar cuántos usuarios están conectados actualmente
        int count = 0;
        for (int i = 0; i < num_users; i++) {
            if (users[i].status == CONNECTED) count++;
        }
        pthread_mutex_unlock(&users_mutex);
        
        // Enviar éxito y la cantidad de usuarios conectados
        send_byte(fd, 0);
        char cstr[16]; snprintf(cstr, sizeof(cstr), "%d", count);
        send_string(fd, cstr);
        
        // Enviar información detallada de cada usuario conectado
        pthread_mutex_lock(&users_mutex);
        for (int i = 0; i < num_users; i++) {
            if (users[i].status == CONNECTED) {
                char uinfo[MAX_NAME + MAX_IP + MAX_PORT + 32];
                snprintf(uinfo, sizeof(uinfo), "%s :: %s :: %s", users[i].name, users[i].ip, users[i].port);
                send_string(fd, uinfo);
            }
        }
        pthread_mutex_unlock(&users_mutex);
    }
}

static void *handle_client(void *arg) {
    // Extraer argumentos pasados por el hilo
    ClientArgs *ca = (ClientArgs *)arg;
    int fd = ca->fd; char ip[MAX_IP]; strcpy(ip, ca->ip); free(ca);
    char op[MAX_NAME];
    // Leer la operación que solicita el cliente
    if (recv_string(fd, op, MAX_NAME) < 0) { close(fd); return NULL; }
    // Despachar a la función correspondiente según el comando
    if (strcmp(op, "REGISTER") == 0) handle_register(fd, ip);
    else if (strcmp(op, "UNREGISTER") == 0) handle_unregister(fd);
    else if (strcmp(op, "CONNECT") == 0) handle_connect(fd, ip);
    else if (strcmp(op, "DISCONNECT") == 0) handle_disconnect(fd, ip);
    else if (strcmp(op, "SEND") == 0) handle_send_generic(fd, 0);
    else if (strcmp(op, "SENDATTACH") == 0) handle_send_generic(fd, 1);
    else if (strcmp(op, "USERS") == 0) handle_users(fd);
    close(fd); return NULL;
}

int main(int argc, char *argv[]) {
    // Validar argumentos de línea de comandos
    if (argc != 3) { fprintf(stderr, "Uso: %s -p <puerto>\n", argv[0]); return 1; }
    int port = atoi(argv[2]);
    // Obtener IP del servidor RPC desde variable de entorno
    rpc_server_ip = getenv("LOG_RPC_IP");
    
    // Crear socket principal del servidor
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    // Permitir reutilizar la dirección para reinicios rápidos
    int opt = 1; setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(port);
    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_fd, BACKLOG);
    
    // Obtener y mostrar IP local del servidor
    char local_ip[MAX_IP]; get_local_ip(local_ip, sizeof(local_ip));
    printf("s> init server %s:%d\n", local_ip, port);
    
    // Bucle principal: aceptar conexiones entrantes
    while (1) {
        struct sockaddr_in caddr; socklen_t clen = sizeof(caddr);
        int cfd = accept(server_fd, (struct sockaddr *)&caddr, &clen);
        // Crear estructura con datos del cliente para el hilo
        ClientArgs *ca = malloc(sizeof(ClientArgs));
        ca->fd = cfd; inet_ntop(AF_INET, &caddr.sin_addr, ca->ip, MAX_IP);
        // Crear hilo para atender al cliente y desprenderse de él
        pthread_t tid; pthread_create(&tid, NULL, handle_client, ca);
        pthread_detach(tid);
    }
    return 0;
}
