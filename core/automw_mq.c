#include "automw_mq.h"

#include <mqueue.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>

/*
 * POSIX message queues are kernel-managed FIFO queues.
 * Unlike shared memory (which is just RAM), message queues
 * give you framing — each mq_send() corresponds to exactly
 * one mq_receive(). No partial reads, no length tracking needed.
 *
 * They live in /dev/mqueue on Linux (mount with -t mqueue to see them).
 * Named with a leading '/' like shared memory.
 *
 * Key difference from pipes:
 * - Pipes are byte streams (no message boundaries)
 * - Message queues preserve message boundaries
 * - Message queues have priority support (we don't use it here)
 *
 * In AUTOSAR terms, this is closer to a queued port than a
 * last-is-best port — every message is delivered in order.
 */

#define REQ_PREFIX "/automw_req_"
#define RSP_PREFIX "/automw_rsp_"
#define MAX_MQ_PATH 64

static void make_req_name(char *out, const char *svc) {
    snprintf(out, MAX_MQ_PATH, "%s%s", REQ_PREFIX, svc);
}

static void make_rsp_name(char *out, const char *svc) {
    snprintf(out, MAX_MQ_PATH, "%s%s", RSP_PREFIX, svc);
}

static struct mq_attr default_attr(void) {
    struct mq_attr attr = {0};
    attr.mq_flags   = 0;
    attr.mq_maxmsg  = AUTOMW_MQ_MAX_DEPTH;
    attr.mq_msgsize = AUTOMW_MQ_MAX_MSG_SIZE;
    return attr;
}

/*
 * automw_server_create()
 *
 * Creates both queues. Server owns creation.
 * O_RDONLY on req queue — server only reads requests.
 * O_WRONLY on rsp queue — server only writes responses.
 *
 * mq_attr controls queue capacity:
 * mq_maxmsg — max messages in queue before mq_send() blocks
 * mq_msgsize — max bytes per message (must match on sender/receiver)
 */
int automw_server_create(const char *service_name) {
    char req[MAX_MQ_PATH], rsp[MAX_MQ_PATH];
    make_req_name(req, service_name);
    make_rsp_name(rsp, service_name);

    struct mq_attr attr = default_attr();

    mqd_t req_q = mq_open(req, O_CREAT | O_RDONLY, 0666, &attr);
    if (req_q == (mqd_t)-1) {
        fprintf(stderr, "[automw] server req queue failed: %s\n",
                strerror(errno));
        return -1;
    }
    mq_close(req_q);

    mqd_t rsp_q = mq_open(rsp, O_CREAT | O_WRONLY, 0666, &attr);
    if (rsp_q == (mqd_t)-1) {
        fprintf(stderr, "[automw] server rsp queue failed: %s\n",
                strerror(errno));
        mq_unlink(req);
        return -1;
    }
    mq_close(rsp_q);

    printf("[automw] service '%s' created\n", service_name);
    return 0;
}

/*
 * automw_server_receive()
 *
 * Blocking receive — server blocks here until a client sends a request.
 * This is the server's main loop call, equivalent to WaitEvent()
 * in AUTOSAR OS extended tasks.
 *
 * mq_receive() returns the number of bytes received.
 * Priority parameter (last arg) can be NULL if you don't need it.
 */
int automw_server_receive(const char *service_name,
                           automw_request_t *out_req)
{
    char req[MAX_MQ_PATH];
    make_req_name(req, service_name);

    mqd_t q = mq_open(req, O_RDONLY);
    if (q == (mqd_t)-1) return -1;

    ssize_t n = mq_receive(q, (char *)out_req,
                            AUTOMW_MQ_MAX_MSG_SIZE, NULL);
    mq_close(q);

    if (n < 0) {
        fprintf(stderr, "[automw] mq_receive failed: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

/*
 * automw_server_reply()
 *
 * Server writes response to the response queue.
 * Client is blocking on this queue waiting for the reply.
 */
int automw_server_reply(const char *service_name,
                         const automw_response_t *resp)
{
    char rsp[MAX_MQ_PATH];
    make_rsp_name(rsp, service_name);

    mqd_t q = mq_open(rsp, O_WRONLY);
    if (q == (mqd_t)-1) return -1;

    int rc = mq_send(q, (const char *)resp,
                     sizeof(automw_response_t), 0);
    mq_close(q);
    return rc;
}

void automw_server_destroy(const char *service_name) {
    char req[MAX_MQ_PATH], rsp[MAX_MQ_PATH];
    make_req_name(req, service_name);
    make_rsp_name(rsp, service_name);
    mq_unlink(req);
    mq_unlink(rsp);
    printf("[automw] service '%s' destroyed\n", service_name);
}

/*
 * automw_client_call()
 *
 * Sends a request and waits for response with a timeout.
 *
 * mq_timedreceive() is the timeout variant of mq_receive().
 * It takes an absolute timespec (not relative!) — so we compute
 * clock_gettime(CLOCK_REALTIME) + timeout_ms.
 *
 * This is important: POSIX timeouts are always absolute.
 * A common bug is passing a relative time — it returns immediately.
 *
 * If timeout expires: returns ETIMEDOUT.
 * Maps to AUTOSAR's E_COM_STOPPED or timeout supervision concept.
 */
int automw_client_call(const char *service_name,
                        const automw_request_t  *req,
                        automw_response_t       *out_resp,
                        unsigned int             timeout_ms)
{
    char req_name[MAX_MQ_PATH], rsp_name[MAX_MQ_PATH];
    make_req_name(req_name, service_name);
    make_rsp_name(rsp_name, service_name);

    /* Send request */
    mqd_t req_q = mq_open(req_name, O_WRONLY);
    if (req_q == (mqd_t)-1) {
        fprintf(stderr, "[automw] client open req failed: %s\n",
                strerror(errno));
        return -1;
    }
    int rc = mq_send(req_q, (const char *)req,
                     AUTOMW_MQ_MAX_MSG_SIZE, 0);
    mq_close(req_q);
    if (rc < 0) return -1;

    /* Wait for response with timeout */
    mqd_t rsp_q = mq_open(rsp_name, O_RDONLY);
    if (rsp_q == (mqd_t)-1) return -1;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }

    ssize_t n = mq_timedreceive(rsp_q, (char *)out_resp,
                                  AUTOMW_MQ_MAX_MSG_SIZE, NULL, &ts);
    mq_close(rsp_q);

    if (n < 0) {
        fprintf(stderr, "[automw] client timeout or error: %s\n",
                strerror(errno));
        return -1;
    }
    return 0;
}