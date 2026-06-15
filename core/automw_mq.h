#ifndef AUTOMW_MQ_H
#define AUTOMW_MQ_H

#include <stdint.h>
#include <stddef.h>

/*
 * Client/Server IPC using POSIX message queues.
 *
 * Each service gets two queues:
 *   /automw_req_<service>  ← client sends request to server
 *   /automw_rsp_<service>  ← server sends response to client
 *
 * This is analogous to AUTOSAR's C/S port interface —
 * a defined operation with in/out arguments and a return value.
 */

#define AUTOMW_MQ_MAX_MSG_SIZE  256
#define AUTOMW_MQ_MAX_DEPTH     8

/* Request message */
typedef struct {
    uint32_t request_id;               /* caller sets this, server echoes it */
    uint8_t  payload[120];             /* operation arguments */
    size_t   payload_len;
} automw_request_t;

/* Response message */
typedef struct {
    uint32_t request_id;               /* echoed from request */
    int32_t  status;                   /* 0 = OK, negative = error */
    uint8_t  payload[116];             /* return values */
    size_t   payload_len;
} automw_response_t;

/* Server side */
int automw_server_create (const char *service_name);
int automw_server_receive(const char *service_name,
                          automw_request_t  *out_req);
int automw_server_reply  (const char *service_name,
                          const automw_response_t *resp);
void automw_server_destroy(const char *service_name);

/* Client side */
int automw_client_call   (const char *service_name,
                          const automw_request_t  *req,
                          automw_response_t       *out_resp,
                          unsigned int             timeout_ms);

#endif