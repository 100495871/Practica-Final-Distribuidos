/*
 * server.c - Servidor concurrente multihilo del servicio de mensajería
 * Sistemas Distribuidos - UC3M - Parte 1
 *
 * Protocolo: una conexión TCP por operación.
 * Todos los campos se envían como cadenas terminadas en '\0'.
 * Los códigos de retorno se envían como 1 byte.
 */

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

/* ===================== CONSTANTES ===================== */
#define MAX_USERS        100
#define MAX_PENDING      100
#define MAX_NAME         256
#define MAX_MSG          256
#define MAX_PORT         16
#define MAX_IP           64
#define BACKLOG          10

/* Estado de usuario */
#define DISCONNECTED     0
#define CONNECTED        1

/* ===================== ESTRUCTURAS ===================== */

/* Mensaje pendiente de entrega */
typedef struct {
    unsigned int id;
    char sender[MAX_NAME];
    char text[MAX_MSG];
} PendingMsg;

/* Entrada de usuario en el servidor */
typedef struct {
    char        name[MAX_NAME];
    int         status;               /* DISCONNECTED | CONNECTED */
    char        ip[MAX_IP];
    char        port[MAX_PORT];
    unsigned int msg_counter;         /* contador de mensajes enviados por este usuario */
    PendingMsg  pending[MAX_PENDING]; /* mensajes pendientes de recibir */
    int         num_pending;
} User;

/* Argumento para el hilo de atención de cliente */
typedef struct {
    int  fd;
    char ip[MAX_IP];
} ClientArgs;

/* ===================== VARIABLES GLOBALES ===================== */

static User           users[MAX_USERS];
static int            num_users = 0;
static pthread_mutex_t users_mutex = PTHREAD_MUTEX_INITIALIZER;
static int            server_fd = -1;

/* ===================== FUNCIONES AUXILIARES DE RED ===================== */

/*
 * send_string: envía una cadena terminada en '\0' por el socket fd.
 * Devuelve 0 si OK, -1 si error.
 */
static int send_string(int fd, const char *s)
{
    int   len = (int)strlen(s) + 1;  /* incluye '\0' */
    int   sent = 0;
    while (sent < len) {
        int r = (int)send(fd, s + sent, len - sent, 0);
        if (r <= 0) return -1;
        sent += r;
    }
    return 0;
}

/*
 * recv_string: recibe una cadena terminada en '\0' del socket fd.
 * Escribe en buf (máx maxlen bytes incluido '\0').
 * Devuelve número de caracteres leídos (sin contar '\0') o -1 si error.
 */
static int recv_string(int fd, char *buf, int maxlen)
{
    int  i = 0;
    char c;
    while (i < maxlen - 1) {
        int r = (int)recv(fd, &c, 1, 0);
        if (r <= 0) return -1;
        if (c == '\0') break;
        buf[i++] = c;
    }
    buf[i] = '\0';
    return i;
}

/*
 * send_byte: envía un byte al socket fd.
 * Devuelve 0 si OK, -1 si error.
 */
static int send_byte(int fd, unsigned char b)
{
    return (send(fd, &b, 1, 0) == 1) ? 0 : -1;
}

/*
 * get_local_ip: obtiene la IP local de la máquina y la escribe en ip_buf.
 */
static void get_local_ip(char *ip_buf, int len)
{
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) < 0) {
        strncpy(ip_buf, "127.0.0.1", len - 1);
        return;
    }
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(hostname, NULL, &hints, &res) == 0 && res) {
        struct sockaddr_in *sin = (struct sockaddr_in *)res->ai_addr;
        inet_ntop(AF_INET, &sin->sin_addr, ip_buf, len);
        freeaddrinfo(res);
    } else {
        strncpy(ip_buf, "127.0.0.1", len - 1);
    }
}

/* ===================== FUNCIONES DE BÚSQUEDA (con mutex) ===================== */

/*
 * find_user: busca un usuario por nombre.
 * PRECONDICIÓN: users_mutex debe estar bloqueado por el llamante.
 * Devuelve el índice si existe, -1 si no.
 */
static int find_user(const char *name)
{
    for (int i = 0; i < num_users; i++) {
        if (strcmp(users[i].name, name) == 0)
            return i;
    }
    return -1;
}

/* ===================== ENVÍO SERVIDOR → CLIENTE (por hilo de escucha) ===================== */

/*
 * send_msg_to_client: conecta al hilo de escucha del cliente y le envía un mensaje.
 * Protocolo (sección 8.6):
 *   "SEND_MESSAGE\0" sender\0 id_str\0 message\0
 * Devuelve 0 si OK, -1 si error de conexión.
 */
static int send_msg_to_client(const char *ip, const char *port,
                               const char *sender, unsigned int id,
                               const char *message)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)atoi(port));
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        close(sock);
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }

    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%u", id);

    send_string(sock, "SEND_MESSAGE");
    send_string(sock, sender);
    send_string(sock, id_str);
    send_string(sock, message);
    close(sock);
    return 0;
}

/*
 * send_ack_to_sender: notifica al remitente que su mensaje fue entregado.
 * Protocolo (sección 8.6):
 *   "SEND_MESS_ACK\0" id_str\0
 */
static void send_ack_to_sender(const char *ip, const char *port, unsigned int id)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)atoi(port));
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        close(sock);
        return;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return;
    }

    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%u", id);

    send_string(sock, "SEND_MESS_ACK");
    send_string(sock, id_str);
    close(sock);
}

/* ===================== MANEJADORES DE OPERACIONES ===================== */

/*
 * handle_register: registra un nuevo usuario (sección 7.2 y 8.1).
 * Protocolo entrada: op ya leído. Leer: username\0
 * Protocolo salida: 1 byte (0=OK, 1=ya existe, 2=error)
 */
static void handle_register(int client_fd)
{
    char username[MAX_NAME];

    if (recv_string(client_fd, username, MAX_NAME) < 0) {
        send_byte(client_fd, 2);
        return;
    }

    pthread_mutex_lock(&users_mutex);

    if (find_user(username) >= 0) {
        /* Ya existe un usuario con ese nombre */
        pthread_mutex_unlock(&users_mutex);
        send_byte(client_fd, 1);
        printf("s> REGISTER %s FAIL\n", username);
        return;
    }

    if (num_users >= MAX_USERS) {
        pthread_mutex_unlock(&users_mutex);
        send_byte(client_fd, 2);
        printf("s> REGISTER %s FAIL\n", username);
        return;
    }

    /* Crear la entrada del usuario */
    User *u = &users[num_users];
    memset(u, 0, sizeof(User));
    strncpy(u->name, username, MAX_NAME - 1);
    u->status      = DISCONNECTED;
    u->msg_counter = 0;
    u->num_pending = 0;
    num_users++;

    pthread_mutex_unlock(&users_mutex);

    send_byte(client_fd, 0);
    printf("s> REGISTER %s OK\n", username);
}

/*
 * handle_unregister: da de baja a un usuario (sección 7.3 y 8.2).
 * Protocolo entrada: username\0
 * Protocolo salida: 1 byte (0=OK, 1=no existe, 2=error)
 */
static void handle_unregister(int client_fd)
{
    char username[MAX_NAME];

    if (recv_string(client_fd, username, MAX_NAME) < 0) {
        send_byte(client_fd, 2);
        return;
    }

    pthread_mutex_lock(&users_mutex);

    int idx = find_user(username);
    if (idx < 0) {
        pthread_mutex_unlock(&users_mutex);
        send_byte(client_fd, 1);
        printf("s> UNREGISTER %s FAIL\n", username);
        return;
    }

    /* Eliminar usuario desplazando el array */
    for (int i = idx; i < num_users - 1; i++) {
        users[i] = users[i + 1];
    }
    num_users--;

    pthread_mutex_unlock(&users_mutex);

    send_byte(client_fd, 0);
    printf("s> UNREGISTER %s OK\n", username);
}

/*
 * handle_connect: conecta un usuario al servicio (sección 7.4 y 8.3).
 * Protocolo entrada: username\0 port\0
 * Protocolo salida: 1 byte (0=OK, 1=no existe, 2=ya conectado, 3=error)
 * Tras OK: envía mensajes pendientes al hilo de escucha del cliente.
 */
static void handle_connect(int client_fd, const char *client_ip)
{
    char username[MAX_NAME];
    char port_str[MAX_PORT];

    if (recv_string(client_fd, username, MAX_NAME) < 0 ||
        recv_string(client_fd, port_str, MAX_PORT) < 0) {
        send_byte(client_fd, 3);
        return;
    }

    pthread_mutex_lock(&users_mutex);

    int idx = find_user(username);
    if (idx < 0) {
        pthread_mutex_unlock(&users_mutex);
        send_byte(client_fd, 1);
        printf("s> CONNECT %s FAIL\n", username);
        return;
    }

    if (users[idx].status == CONNECTED) {
        pthread_mutex_unlock(&users_mutex);
        send_byte(client_fd, 2);
        printf("s> CONNECT %s FAIL\n", username);
        return;
    }

    /* Actualizar estado del usuario */
    users[idx].status = CONNECTED;
    strncpy(users[idx].ip,   client_ip, MAX_IP   - 1);
    strncpy(users[idx].port, port_str,  MAX_PORT - 1);

    /* Copiar y limpiar mensajes pendientes */
    int n_pending = users[idx].num_pending;
    PendingMsg pending_copy[MAX_PENDING];
    memcpy(pending_copy, users[idx].pending, (size_t)n_pending * sizeof(PendingMsg));
    users[idx].num_pending = 0;

    pthread_mutex_unlock(&users_mutex);

    /* Enviar OK al cliente */
    send_byte(client_fd, 0);
    printf("s> CONNECT %s OK\n", username);

    /* Entregar mensajes pendientes uno a uno */
    for (int i = 0; i < n_pending; i++) {
        int ok = send_msg_to_client(client_ip, port_str,
                                     pending_copy[i].sender,
                                     pending_copy[i].id,
                                     pending_copy[i].text);
        if (ok == 0) {
            printf("s> SEND MESSAGE %u FROM %s TO %s\n",
                   pending_copy[i].id, pending_copy[i].sender, username);

            /* Notificar al remitente si está conectado */
            pthread_mutex_lock(&users_mutex);
            int sidx = find_user(pending_copy[i].sender);
            if (sidx >= 0 && users[sidx].status == CONNECTED) {
                char sip[MAX_IP], sport[MAX_PORT];
                strncpy(sip,   users[sidx].ip,   MAX_IP   - 1);
                strncpy(sport, users[sidx].port,  MAX_PORT - 1);
                pthread_mutex_unlock(&users_mutex);
                send_ack_to_sender(sip, sport, pending_copy[i].id);
            } else {
                pthread_mutex_unlock(&users_mutex);
            }
        }
        /* Si falla la entrega, el mensaje ya no está en pending (se pierde en esta versión) */
    }
}

/*
 * handle_disconnect: desconecta un usuario (sección 7.5 y 8.4).
 * Protocolo entrada: username\0
 * Protocolo salida: 1 byte (0=OK, 1=no existe, 2=no conectado, 3=error)
 */
static void handle_disconnect(int client_fd)
{
    char username[MAX_NAME];

    if (recv_string(client_fd, username, MAX_NAME) < 0) {
        send_byte(client_fd, 3);
        return;
    }

    pthread_mutex_lock(&users_mutex);

    int idx = find_user(username);
    if (idx < 0) {
        pthread_mutex_unlock(&users_mutex);
        send_byte(client_fd, 1);
        printf("s> DISCONNECT %s FAIL\n", username);
        return;
    }

    if (users[idx].status == DISCONNECTED) {
        pthread_mutex_unlock(&users_mutex);
        send_byte(client_fd, 2);
        printf("s> DISCONNECT %s FAIL\n", username);
        return;
    }

    /* Marcar como desconectado y borrar IP/puerto */
    users[idx].status = DISCONNECTED;
    memset(users[idx].ip,   0, MAX_IP);
    memset(users[idx].port, 0, MAX_PORT);

    pthread_mutex_unlock(&users_mutex);

    send_byte(client_fd, 0);
    printf("s> DISCONNECT %s OK\n", username);
}

/*
 * handle_send: procesa el envío de un mensaje entre clientes (sección 7.6 y 8.5).
 * Protocolo entrada: sender\0 receiver\0 message\0
 * Protocolo salida: 1 byte + (si OK) id_str\0
 */
static void handle_send(int client_fd)
{
    char sender[MAX_NAME], receiver[MAX_NAME], message[MAX_MSG];

    if (recv_string(client_fd, sender,   MAX_NAME) < 0 ||
        recv_string(client_fd, receiver, MAX_NAME) < 0 ||
        recv_string(client_fd, message,  MAX_MSG)  < 0) {
        send_byte(client_fd, 2);
        return;
    }

    pthread_mutex_lock(&users_mutex);

    int sidx = find_user(sender);
    int ridx = find_user(receiver);

    if (ridx < 0) {
        /* Usuario destinatario no existe */
        pthread_mutex_unlock(&users_mutex);
        send_byte(client_fd, 1);
        return;
    }

    if (sidx < 0) {
        /* Usuario remitente no existe */
        pthread_mutex_unlock(&users_mutex);
        send_byte(client_fd, 2);
        return;
    }

    /* Asignar ID de mensaje (contador del remitente, sección 6.6) */
    users[sidx].msg_counter++;
    if (users[sidx].msg_counter == 0) {
        /* Overflow: la siguiente ID válida es 1 (sección 6.6) */
        users[sidx].msg_counter = 1;
    }
    unsigned int msg_id = users[sidx].msg_counter;

    /* Comprobar si el receptor está conectado */
    int recv_connected = (users[ridx].status == CONNECTED);
    char recv_ip[MAX_IP], recv_port[MAX_PORT];
    if (recv_connected) {
        strncpy(recv_ip,   users[ridx].ip,   MAX_IP   - 1);
        strncpy(recv_port, users[ridx].port,  MAX_PORT - 1);
    } else {
        /* Almacenar mensaje pendiente */
        if (users[ridx].num_pending < MAX_PENDING) {
            PendingMsg *pm = &users[ridx].pending[users[ridx].num_pending];
            pm->id = msg_id;
            strncpy(pm->sender, sender,  MAX_NAME - 1);
            strncpy(pm->text,   message, MAX_MSG  - 1);
            users[ridx].num_pending++;
        }
    }

    /* Comprobar si el remitente está conectado (para el ACK) */
    int sender_connected = (users[sidx].status == CONNECTED);
    char sender_ip[MAX_IP], sender_port[MAX_PORT];
    if (sender_connected) {
        strncpy(sender_ip,   users[sidx].ip,   MAX_IP   - 1);
        strncpy(sender_port, users[sidx].port,  MAX_PORT - 1);
    }

    pthread_mutex_unlock(&users_mutex);

    /* Responder al remitente con OK + ID */
    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%u", msg_id);
    send_byte(client_fd, 0);
    send_string(client_fd, id_str);

    /* Entregar o almacenar según estado del receptor */
    if (recv_connected) {
        int ok = send_msg_to_client(recv_ip, recv_port,
                                     sender, msg_id, message);
        if (ok == 0) {
            printf("s> SEND MESSAGE %u FROM %s TO %s\n", msg_id, sender, receiver);
            /* Notificar al remitente */
            if (sender_connected) {
                send_ack_to_sender(sender_ip, sender_port, msg_id);
            }
        } else {
            /* Error al enviar: marcar receptor como desconectado (sección 8.6) */
            pthread_mutex_lock(&users_mutex);
            int r = find_user(receiver);
            if (r >= 0) {
                users[r].status = DISCONNECTED;
                memset(users[r].ip,   0, MAX_IP);
                memset(users[r].port, 0, MAX_PORT);
                /* Re-encolar el mensaje */
                if (users[r].num_pending < MAX_PENDING) {
                    PendingMsg *pm = &users[r].pending[users[r].num_pending];
                    pm->id = msg_id;
                    strncpy(pm->sender, sender,  MAX_NAME - 1);
                    strncpy(pm->text,   message, MAX_MSG  - 1);
                    users[r].num_pending++;
                }
            }
            pthread_mutex_unlock(&users_mutex);
            printf("s> MESSAGE %u FROM %s TO %s STORED\n", msg_id, sender, receiver);
        }
    } else {
        printf("s> MESSAGE %u FROM %s TO %s STORED\n", msg_id, sender, receiver);
    }
}

/*
 * handle_users: lista usuarios conectados (sección 7.7 y 8.7).
 * Protocolo entrada: username\0
 * Protocolo salida: 1 byte + (si OK) count_str\0 + count×name\0
 */
static void handle_users(int client_fd)
{
    char username[MAX_NAME];

    if (recv_string(client_fd, username, MAX_NAME) < 0) {
        send_byte(client_fd, 2);
        printf("s> CONNECTEDUSERS FAIL\n");
        return;
    }

    pthread_mutex_lock(&users_mutex);

    int idx = find_user(username);
    if (idx < 0 || users[idx].status == DISCONNECTED) {
        pthread_mutex_unlock(&users_mutex);
        send_byte(client_fd, 1);
        printf("s> CONNECTEDUSERS FAIL\n");
        return;
    }

    /* Recopilar usuarios conectados */
    char connected[MAX_USERS][MAX_NAME];
    int count = 0;
    for (int i = 0; i < num_users; i++) {
        if (users[i].status == CONNECTED) {
            strncpy(connected[count], users[i].name, MAX_NAME - 1);
            count++;
        }
    }

    pthread_mutex_unlock(&users_mutex);

    char count_str[16];
    snprintf(count_str, sizeof(count_str), "%d", count);

    send_byte(client_fd, 0);
    send_string(client_fd, count_str);
    for (int i = 0; i < count; i++) {
        send_string(client_fd, connected[i]);
    }
    printf("s> CONNECTEDUSERS OK\n");
}

/* ===================== HILO PRINCIPAL DE ATENCIÓN AL CLIENTE ===================== */

/*
 * handle_client: hilo que atiende una conexión de cliente.
 * Lee la operación y llama al manejador correspondiente.
 */
static void *handle_client(void *arg)
{
    ClientArgs *ca = (ClientArgs *)arg;
    int  fd = ca->fd;
    char ip[MAX_IP];
    strncpy(ip, ca->ip, MAX_IP - 1);
    free(ca);

    char op[MAX_NAME];
    if (recv_string(fd, op, MAX_NAME) < 0) {
        close(fd);
        return NULL;
    }

    if      (strcmp(op, "REGISTER")   == 0) handle_register(fd);
    else if (strcmp(op, "UNREGISTER") == 0) handle_unregister(fd);
    else if (strcmp(op, "CONNECT")    == 0) handle_connect(fd, ip);
    else if (strcmp(op, "DISCONNECT") == 0) handle_disconnect(fd);
    else if (strcmp(op, "SEND")       == 0) handle_send(fd);
    else if (strcmp(op, "USERS")      == 0) handle_users(fd);
    else {
        fprintf(stderr, "Operación desconocida: %s\n", op);
    }

    close(fd);
    return NULL;
}

/* ===================== MANEJADOR DE SEÑAL ===================== */

static void handle_sigint(int sig)
{
    (void)sig;
    printf("\ns> Servidor terminando...\n");
    if (server_fd >= 0)
        close(server_fd);
    exit(0);
}

/* ===================== MAIN ===================== */

int main(int argc, char *argv[])
{
    /* Procesar argumentos: ./server -p <port> */
    if (argc != 3 || strcmp(argv[1], "-p") != 0) {
        fprintf(stderr, "Uso: %s -p <puerto>\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[2]);
    if (port < 1024 || port > 65535) {
        fprintf(stderr, "Error: puerto fuera de rango [1024, 65535]\n");
        return 1;
    }

    /* Instalar manejador de SIGINT */
    signal(SIGINT, handle_sigint);

    /* Crear socket del servidor */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    /* Mostrar IP y puerto locales */
    char local_ip[MAX_IP];
    get_local_ip(local_ip, sizeof(local_ip));
    printf("s> init server %s:%d\n", local_ip, port);
    printf("s> \n");
    fflush(stdout);

    /* Bucle de aceptación de conexiones */
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        /* Crear hilo de atención */
        ClientArgs *ca = malloc(sizeof(ClientArgs));
        if (!ca) {
            close(client_fd);
            continue;
        }
        ca->fd = client_fd;
        inet_ntop(AF_INET, &client_addr.sin_addr, ca->ip, MAX_IP);

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, ca) != 0) {
            perror("pthread_create");
            free(ca);
            close(client_fd);
            continue;
        }
        pthread_detach(tid);
    }

    return 0;
}