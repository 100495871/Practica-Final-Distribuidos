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

/* ===================== CONSTANTES ===================== */
#define MAX_USERS        100
#define MAX_PENDING      100
#define MAX_NAME         256
#define MAX_MSG          256
#define MAX_PORT         16
#define MAX_IP           64
#define BACKLOG          10

#define DISCONNECTED     0
#define CONNECTED        1

/* ===================== ESTRUCTURAS ===================== */

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

/* ===================== VARIABLES GLOBALES ===================== */

static User           users[MAX_USERS];
static int            num_users = 0;
static pthread_mutex_t users_mutex = PTHREAD_MUTEX_INITIALIZER;
static int            server_fd = -1;
static char           *rpc_server_ip = NULL;

/* ===================== RPC CLIENT ===================== */

static void call_rpc_log(const char *user, const char *op, const char *filename) {
    if (!rpc_server_ip) return;
    
    CLIENT *clnt;
    enum clnt_stat status;
    int result;
    
    clnt = clnt_create(rpc_server_ip, LOGPROG, LOGVERS, "tcp");
    if (clnt == NULL) {
        return;
    }
    
    if (filename == NULL) {
        struct log_operation_args args;
        args.user = (char *)user;
        args.operation = (char *)op;
        status = log_operation_1(&args, &result, clnt);
    } else {
        struct log_sendattach_args args;
        args.user = (char *)user;
        args.operation = (char *)op;
        args.filename = (char *)filename;
        status = log_sendattach_1(&args, &result, clnt);
    }
    
    if (status != RPC_SUCCESS) {
        // Error en la comunicación RPC
    }
    
    clnt_destroy(clnt);
}

/* ===================== FUNCIONES AUXILIARES DE RED ===================== */

static int send_string(int fd, const char *s) {
    int   len = (int)strlen(s) + 1;
    int   sent = 0;
    while (sent < len) {
        int r = (int)send(fd, s + sent, len - sent, 0);
        if (r <= 0) return -1;
        sent += r;
    }
    return 0;
}

static int recv_string(int fd, char *buf, int maxlen) {
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

static int send_byte(int fd, unsigned char b) {
    return (send(fd, &b, 1, 0) == 1) ? 0 : -1;
}

static void get_local_ip(char *ip_buf, int len) {
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

static int find_user(const char *name) {
    for (int i = 0; i < num_users; i++) {
        if (strcmp(users[i].name, name) == 0) return i;
    }
    return -1;
}

/* ===================== ENVÍO SERVIDOR → CLIENTE ===================== */

static int deliver_message(const char *ip, const char *port, const char *sender, 
                          unsigned int id, const char *message, const char *filename) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)atoi(port));
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }

    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%u", id);

    if (filename == NULL) {
        send_string(sock, "SEND_MESSAGE");
        send_string(sock, sender);
        send_string(sock, id_str);
        send_string(sock, message);
    } else {
        send_string(sock, "SEND_MESSAGE_ATTACH");
        send_string(sock, sender);
        send_string(sock, id_str);
        send_string(sock, message);
        send_string(sock, filename);
    }
    close(sock);
    return 0;
}

static void send_ack_to_sender(const char *ip, const char *port, unsigned int id, const char *filename) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)atoi(port));
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return;
    }

    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%u", id);

    if (filename == NULL) {
        send_string(sock, "SEND_MESS_ACK");
        send_string(sock, id_str);
    } else {
        send_string(sock, "SEND_MESS_ATTACH_ACK");
        send_string(sock, id_str);
        send_string(sock, filename);
    }
    close(sock);
}

/* ===================== MANEJADORES DE OPERACIONES ===================== */

static void handle_register(int fd, const char *ip) {
    char name[MAX_NAME];
    recv_string(fd, name, MAX_NAME);
    call_rpc_log(name, "REGISTER", NULL);
    
    pthread_mutex_lock(&users_mutex);
    if (find_user(name) >= 0 || num_users >= MAX_USERS) {
        pthread_mutex_unlock(&users_mutex);
        send_byte(fd, find_user(name) >= 0 ? 1 : 2);
        printf("s> REGISTER %s FAIL\n", name);
    } else {
        User *u = &users[num_users++];
        memset(u, 0, sizeof(User));
        strncpy(u->name, name, MAX_NAME - 1);
        strncpy(u->register_ip, ip, MAX_IP - 1);
        u->status = DISCONNECTED;
        pthread_mutex_unlock(&users_mutex);
        send_byte(fd, 0);
        printf("s> REGISTER %s OK\n", name);
    }
}

static void handle_unregister(int fd) {
    char name[MAX_NAME];
    recv_string(fd, name, MAX_NAME);
    call_rpc_log(name, "UNREGISTER", NULL);
    
    pthread_mutex_lock(&users_mutex);
    int idx = find_user(name);
    if (idx < 0) {
        pthread_mutex_unlock(&users_mutex);
        send_byte(fd, 1);
        printf("s> UNREGISTER %s FAIL\n", name);
    } else {
        /* Secc 7.3: Borrar entrada */
        for (int i = idx; i < num_users - 1; i++) users[i] = users[i+1];
        num_users--;
        pthread_mutex_unlock(&users_mutex);
        send_byte(fd, 0);
        printf("s> UNREGISTER %s OK\n", name);
    }
}

static void handle_connect(int fd, const char *ip) {
    char name[MAX_NAME], port[MAX_PORT];
    recv_string(fd, name, MAX_NAME);
    recv_string(fd, port, MAX_PORT);
    call_rpc_log(name, "CONNECT", NULL);
    
    pthread_mutex_lock(&users_mutex);
    int idx = find_user(name);
    if (idx < 0) {
        pthread_mutex_unlock(&users_mutex);
        send_byte(fd, 1);
        printf("s> CONNECT %s FAIL\n", name);
    } else if (users[idx].status == CONNECTED) {
        pthread_mutex_unlock(&users_mutex);
        send_byte(fd, 2);
        printf("s> CONNECT %s FAIL\n", name);
    } else {
        users[idx].status = CONNECTED;
        strncpy(users[idx].ip, ip, MAX_IP-1);
        strncpy(users[idx].port, port, MAX_PORT-1);
        
        /* Entregar mensajes pendientes uno por uno */
        int ok_delivered[MAX_PENDING];
        for (int i = 0; i < users[idx].num_pending; i++) {
            // Nota: El protocolo dice que si falla la entrega se mantiene en el servidor
            if (deliver_message(ip, port, users[idx].pending[i].sender, users[idx].pending[i].id, 
                                 users[idx].pending[i].text, users[idx].pending[i].has_attach ? users[idx].pending[i].filename : NULL) == 0) {
                ok_delivered[i] = 1;
                printf("s> SEND MESSAGE %u FROM %s TO %s\n", users[idx].pending[i].id, users[idx].pending[i].sender, name);
                
                // Notificar remitente
                int sidx = find_user(users[idx].pending[i].sender);
                if (sidx >= 0 && users[sidx].status == CONNECTED) {
                    send_ack_to_sender(users[sidx].ip, users[sidx].port, users[idx].pending[i].id, users[idx].pending[i].has_attach ? users[idx].pending[i].filename : NULL);
                }
            } else {
                ok_delivered[i] = 0;
                // Desconexión detectada
                users[idx].status = DISCONNECTED;
                break;
            }
        }
        
        // Limpiar solo los entregados
        int j = 0;
        for (int i = 0; i < users[idx].num_pending; i++) {
            if (!ok_delivered[i]) {
                users[idx].pending[j++] = users[idx].pending[i];
            }
        }
        users[idx].num_pending = j;
        
        pthread_mutex_unlock(&users_mutex);
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
    if (idx < 0 || users[idx].status == DISCONNECTED) {
        pthread_mutex_unlock(&users_mutex);
        send_byte(fd, idx < 0 ? 1 : 2);
        printf("s> DISCONNECT %s FAIL\n", name);
        return;
    }
    
    /* Secc 8.4: solo puede desconectarse desde la IP desde la que se registró */
    if (strcmp(users[idx].register_ip, ip) != 0) {
        pthread_mutex_unlock(&users_mutex);
        send_byte(fd, 3);
        printf("s> DISCONNECT %s FAIL (IP mismatch)\n", name);
        return;
    }

    users[idx].status = DISCONNECTED;
    pthread_mutex_unlock(&users_mutex);
    send_byte(fd, 0);
    printf("s> DISCONNECT %s OK\n", name);
}

static void handle_send_generic(int fd, int has_attach) {
    char sender[MAX_NAME], receiver[MAX_NAME], message[MAX_MSG], filename[MAX_NAME];
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
    
    if (ridx < 0) { pthread_mutex_unlock(&users_mutex); send_byte(fd, 1); return; }
    if (sidx < 0) { pthread_mutex_unlock(&users_mutex); send_byte(fd, 2); return; }
    
    unsigned int msg_id = ++users[sidx].msg_counter;
    if (msg_id == 0) msg_id = users[sidx].msg_counter = 1;

    int r_conn = (users[ridx].status == CONNECTED);
    char rip[MAX_IP], rport[MAX_PORT];
    if (r_conn) { strcpy(rip, users[ridx].ip); strcpy(rport, users[ridx].port); }
    else if (users[ridx].num_pending < MAX_PENDING) {
        PendingMsg *pm = &users[ridx].pending[users[ridx].num_pending++];
        pm->id = msg_id; pm->has_attach = has_attach;
        strcpy(pm->sender, sender); strcpy(pm->text, message);
        if (has_attach) strcpy(pm->filename, filename);
    }
    
    int s_conn = (users[sidx].status == CONNECTED);
    char sip[MAX_IP], sport[MAX_PORT];
    if (s_conn) { strcpy(sip, users[sidx].ip); strcpy(sport, users[sidx].port); }
    pthread_mutex_unlock(&users_mutex);
    
    char id_str[32]; snprintf(id_str, sizeof(id_str), "%u", msg_id);
    send_byte(fd, 0);
    send_string(fd, id_str);
    
    if (r_conn) {
        if (deliver_message(rip, rport, sender, msg_id, message, has_attach ? filename : NULL) == 0) {
            if (s_conn) send_ack_to_sender(sip, sport, msg_id, has_attach ? filename : NULL);
        } else {
            // Se desconectó el destino durante el envío
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
    if (idx < 0 || users[idx].status == DISCONNECTED) {
        pthread_mutex_unlock(&users_mutex); send_byte(fd, 1);
    } else {
        int count = 0;
        for (int i = 0; i < num_users; i++) {
            if (users[i].status == CONNECTED) count++;
        }
        pthread_mutex_unlock(&users_mutex);
        
        send_byte(fd, 0);
        char cstr[16]; snprintf(cstr, sizeof(cstr), "%d", count);
        send_string(fd, cstr);
        
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
    ClientArgs *ca = (ClientArgs *)arg;
    int fd = ca->fd; char ip[MAX_IP]; strcpy(ip, ca->ip); free(ca);
    char op[MAX_NAME];
    if (recv_string(fd, op, MAX_NAME) < 0) { close(fd); return NULL; }
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
    if (argc != 3) { fprintf(stderr, "Uso: %s -p <puerto>\n", argv[0]); return 1; }
    int port = atoi(argv[2]);
    rpc_server_ip = getenv("LOG_RPC_IP");
    
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1; setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(port);
    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_fd, BACKLOG);
    
    char local_ip[MAX_IP]; get_local_ip(local_ip, sizeof(local_ip));
    printf("s> init server %s:%d\n", local_ip, port);
    
    while (1) {
        struct sockaddr_in caddr; socklen_t clen = sizeof(caddr);
        int cfd = accept(server_fd, (struct sockaddr *)&caddr, &clen);
        ClientArgs *ca = malloc(sizeof(ClientArgs));
        ca->fd = cfd; inet_ntop(AF_INET, &caddr.sin_addr, ca->ip, MAX_IP);
        pthread_t tid; pthread_create(&tid, NULL, handle_client, ca);
        pthread_detach(tid);
    }
    return 0;
}
