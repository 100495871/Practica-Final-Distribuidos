#include <stdio.h>
#include <stdlib.h>
#include "log.h"

extern void logprog_1(struct svc_req *rqstp, SVCXPRT *transp);

bool_t log_operation_1_svc(struct log_operation_args *args, int *result, struct svc_req *req) {
    (void)req;
    printf("%s %s\n", args->user, args->operation);
    fflush(stdout);
    *result = 0;
    return TRUE;
}

bool_t log_sendattach_1_svc(struct log_sendattach_args *args, int *result, struct svc_req *req) {
    (void)req;
    printf("%s %s %s\n", args->user, args->operation, args->filename);
    fflush(stdout);
    *result = 0;
    return TRUE;
}

int logprog_1_freeresult(SVCXPRT *transp, xdrproc_t xdr_result, caddr_t result) {
    (void)transp;
    xdr_free(xdr_result, result);
    return 1;
}

int main(void)
{
    SVCXPRT *transp;

    /* Limpiar registros anteriores del portmapper */
    pmap_unset(LOGPROG, LOGVERS);

    /* Crear transporte TCP */
    transp = svctcp_create(RPC_ANYSOCK, 0, 0);
    if (transp == NULL) {
        fprintf(stderr, "rpc_server: no se pudo crear el servicio TCP\n");
        exit(1);
    }

    /* Registrar el programa con el portmapper */
    if (!svc_register(transp, LOGPROG, LOGVERS, logprog_1, IPPROTO_TCP)) {
        fprintf(stderr, "rpc_server: no se pudo registrar (LOGPROG, LOGVERS, tcp)\n");
        exit(1);
    }

    printf("s> Servidor RPC de registro iniciado (LOGPROG=0x%x, LOGVERS=%d)\n",
           LOGPROG, LOGVERS);
    fflush(stdout);

    svc_run();   /* bucle de eventos RPC — no retorna en condiciones normales */
    fprintf(stderr, "rpc_server: svc_run terminó inesperadamente\n");
    exit(1);
}