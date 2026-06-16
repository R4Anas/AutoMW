#include "someip.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

/*
 * UDP socket basics — SOME/IP transport layer
 *
 * UDP is connectionless — no handshake, no guaranteed delivery
 * Each sendto() = one datagram = one someip_msg_t on the wire
 * recvfrom() gives you one complete datagram = one someip_msg_t
 * Message boundaries are preserved — unlike TCP which is a byte stream
 *
 * AUTOSAR parallel:
 * Like a CAN frame — fire and forget, no ACK at transport level
 * SOME/IP itself adds REQUEST/RESPONSE at application level
 * just like UDS adds request/response on top of CAN transport
 *
 * Why UDP not TCP for SOME/IP?
 * - Lower latency — no connection setup
 * - No head-of-line blocking — one lost frame doesn't stall others
 * - Multicast support — SD needs multicast, TCP can't do multicast
 * TCP is used for large payloads (OTA, large data transfers) in SOME/IP
 * but UDP is the default for service calls and notifications
 */

/*
 * someip_create_socket()
 *
 * Creates a UDP socket and binds it to a port.
 * SO_REUSEADDR — allows restarting server without "address already in use"
 * error. Without it you'd have to wait ~60 seconds (TIME_WAIT state)
 * after killing the server before restarting.
 * In embedded Linux this is standard practice for any server socket.
 *
 * [POSIX API] socket() — creates socket file descriptor
 * AF_INET     = IPv4
 * SOCK_DGRAM  = UDP (datagram)
 * 0           = default protocol for this socket type
 */
int someip_create_socket(uint16_t port) {
    /* [POSIX API — kernel syscall] */
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        fprintf(stderr, "[someip] socket() failed: %s\n", strerror(errno));
        return -1;
    }

    /* [POSIX API] Allow reuse of port immediately after restart */
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* [POSIX API] Bind to port — tells kernel to deliver
     * datagrams sent to this port to our socket */
    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);   /* [API] host→network byte order */
    addr.sin_addr.s_addr = INADDR_ANY;    /* accept from any interface */

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[someip] bind() port %u failed: %s\n",
                port, strerror(errno));
        close(sock);
        return -1;
    }

    printf("[someip] socket created on port %u\n", port);
    return sock;
}

/*
 * someip_send()
 *
 * Serializes a someip_msg_t to wire format and sends via UDP.
 *
 * Wire format = header (16 bytes, big-endian) + payload (variable)
 *
 * Key operation: byte order conversion
 * x86 Linux is little-endian — least significant byte at lowest address
 * Network (SOME/IP) is big-endian — most significant byte first
 *
 * Example: uint16_t service_id = 0x0101
 * Little-endian in memory: [0x01][0x01] (happens to be same here)
 * Example: uint32_t length = 0x00000008
 * Little-endian: [0x08][0x00][0x00][0x00]
 * Big-endian:    [0x00][0x00][0x00][0x08] ← what wire must have
 *
 * htonl() = "host to network long" = converts uint32_t
 * htons() = "host to network short" = converts uint16_t
 * Miss one field and Wireshark shows garbage — common real bug
 */
int someip_send(int sock,
                const char *dest_ip,
                uint16_t dest_port,
                const someip_msg_t *msg)
{
    /* Build wire buffer — header + payload */
    uint8_t wire[SOMEIP_HEADER_SIZE + SOMEIP_MAX_PAYLOAD];
    memset(wire, 0, sizeof(wire));

    /*
     * Serialize header to big-endian
     * __attribute__((packed)) prevents padding but doesn't fix endianness
     * We must convert each multi-byte field manually
     */
    someip_header_t *h = (someip_header_t *)wire;
    h->service_id  = htons(msg->header.service_id);   /* [API] 16-bit swap */
    h->method_id   = htons(msg->header.method_id);
    h->length      = htonl(msg->header.length);        /* [API] 32-bit swap */
    h->client_id   = htons(msg->header.client_id);
    h->session_id  = htons(msg->header.session_id);
    h->proto_ver   = msg->header.proto_ver;    /* single bytes — no swap */
    h->iface_ver   = msg->header.iface_ver;
    h->msg_type    = msg->header.msg_type;
    h->return_code = msg->header.return_code;

    /* Append payload after header */
    if (msg->payload_len > 0) {
        memcpy(wire + SOMEIP_HEADER_SIZE,
               msg->payload,
               msg->payload_len);
    }

    size_t total_len = SOMEIP_HEADER_SIZE + msg->payload_len;

    /* Build destination address */
    struct sockaddr_in dest = {0};
    dest.sin_family = AF_INET;
    dest.sin_port   = htons(dest_port);

    /* [POSIX API] inet_pton() converts "127.0.0.1" string to binary */
    if (inet_pton(AF_INET, dest_ip, &dest.sin_addr) <= 0) {
        fprintf(stderr, "[someip] invalid IP: %s\n", dest_ip);
        return -1;
    }

    /*
     * [POSIX API — kernel syscall] sendto()
     * Sends datagram to specific destination.
     * Unlike send() which requires a connected socket,
     * sendto() specifies destination per-call — correct for UDP
     */
    ssize_t sent = sendto(sock, wire, total_len, 0,
                          (struct sockaddr *)&dest, sizeof(dest));
    if (sent < 0) {
        fprintf(stderr, "[someip] sendto() failed: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

/*
 * someip_receive()
 *
 * Receives a UDP datagram and deserializes into someip_msg_t.
 * Reverse of someip_send() — ntohl()/ntohs() convert back to host order.
 *
 * recvfrom() is blocking — process suspends until datagram arrives.
 * Same blocking behavior as mq_receive() in Demo 2.
 *
 * out_src_ip and out_src_port tell you who sent it —
 * needed by server to send response back to the right client.
 * UDP has no persistent connection so you must track sender address manually.
 * TCP would give you a persistent connection per client — no need to track.
 */
int someip_receive(int sock,
                   someip_msg_t *out_msg,
                   char *out_src_ip,
                   uint16_t *out_src_port)
{
    uint8_t wire[SOMEIP_HEADER_SIZE + SOMEIP_MAX_PAYLOAD];
    struct sockaddr_in src = {0};
    socklen_t src_len = sizeof(src);

    /*
     * [POSIX API — kernel syscall] recvfrom()
     * Blocking receive — suspends until datagram arrives.
     * Fills src with sender's IP and port.
     */
    ssize_t n = recvfrom(sock, wire, sizeof(wire), 0,
                          (struct sockaddr *)&src, &src_len);
    if (n < 0) {
        fprintf(stderr, "[someip] recvfrom() failed: %s\n", strerror(errno));
        return -1;
    }

    if (n < SOMEIP_HEADER_SIZE) {
        fprintf(stderr, "[someip] datagram too short: %zd bytes\n", n);
        return -1;
    }

    /* Deserialize header — convert from big-endian to host order */
    someip_header_t *h = (someip_header_t *)wire;
    out_msg->header.service_id  = ntohs(h->service_id);  /* [API] */
    out_msg->header.method_id   = ntohs(h->method_id);
    out_msg->header.length      = ntohl(h->length);       /* [API] */
    out_msg->header.client_id   = ntohs(h->client_id);
    out_msg->header.session_id  = ntohs(h->session_id);
    out_msg->header.proto_ver   = h->proto_ver;
    out_msg->header.iface_ver   = h->iface_ver;
    out_msg->header.msg_type    = h->msg_type;
    out_msg->header.return_code = h->return_code;

    /* Extract payload */
    out_msg->payload_len = n - SOMEIP_HEADER_SIZE;
    if (out_msg->payload_len > 0) {
        memcpy(out_msg->payload,
               wire + SOMEIP_HEADER_SIZE,
               out_msg->payload_len);
    }

    /* Extract sender address — server needs this to reply */
    if (out_src_ip) {
        /* [POSIX API] inet_ntop() converts binary IP to string */
        inet_ntop(AF_INET, &src.sin_addr, out_src_ip, INET_ADDRSTRLEN);
    }
    if (out_src_port) {
        *out_src_port = ntohs(src.sin_port);
    }

    return 0;
}

/*
 * someip_build_request()
 *
 * Fills a someip_msg_t for a REQUEST message.
 * length field = bytes after length field itself
 *             = remaining header (8 bytes) + payload
 *
 * length calculation:
 * Total header = 16 bytes
 * length field is at offset 4, covers bytes 8..15 + payload
 * So length = 8 (remaining header) + payload_len
 */
void someip_build_request(someip_msg_t *msg,
                           uint16_t service_id,
                           uint16_t method_id,
                           uint16_t client_id,
                           uint16_t session_id,
                           const void *payload,
                           size_t payload_len)
{
    memset(msg, 0, sizeof(*msg));
    msg->header.service_id  = service_id;
    msg->header.method_id   = method_id;
    msg->header.length      = 8 + (uint32_t)payload_len;
    msg->header.client_id   = client_id;
    msg->header.session_id  = session_id;
    msg->header.proto_ver   = SOMEIP_PROTO_VER;
    msg->header.iface_ver   = SOMEIP_IFACE_VER;
    msg->header.msg_type    = SOMEIP_MSG_REQUEST;
    msg->header.return_code = SOMEIP_RET_OK;

    if (payload && payload_len > 0) {
        memcpy(msg->payload, payload, payload_len);
        msg->payload_len = payload_len;
    }
}

/*
 * someip_build_response()
 *
 * Builds a RESPONSE echoing client_id and session_id from request.
 * This is how client matches response to its original request —
 * same correlation concept as request_id in AutoMW message queues.
 */
void someip_build_response(someip_msg_t *msg,
                            const someip_msg_t *req,
                            uint8_t return_code,
                            const void *payload,
                            size_t payload_len)
{
    memset(msg, 0, sizeof(*msg));
    msg->header.service_id  = req->header.service_id;
    msg->header.method_id   = req->header.method_id;
    msg->header.length      = 8 + (uint32_t)payload_len;
    msg->header.client_id   = req->header.client_id;   /* echo back */
    msg->header.session_id  = req->header.session_id;  /* echo back */
    msg->header.proto_ver   = SOMEIP_PROTO_VER;
    msg->header.iface_ver   = SOMEIP_IFACE_VER;
    msg->header.msg_type    = SOMEIP_MSG_RESPONSE;
    msg->header.return_code = return_code;

    if (payload && payload_len > 0) {
        memcpy(msg->payload, payload, payload_len);
        msg->payload_len = payload_len;
    }
}

/*
 * someip_build_notification()
 *
 * Builds a NOTIFICATION — server pushing event to subscribers.
 * client_id = 0x0000, session_id increments per notification.
 * method_id for events is in the 0x8000+ range by SOME/IP convention.
 *
 * AUTOSAR parallel: sender/receiver event port notification
 * vs request/response which is C/S port
 */
void someip_build_notification(someip_msg_t *msg,
                                uint16_t service_id,
                                uint16_t event_id,
                                const void *payload,
                                size_t payload_len)
{
    static uint16_t session = 0;
    memset(msg, 0, sizeof(*msg));
    msg->header.service_id  = service_id;
    msg->header.method_id   = event_id;
    msg->header.length      = 8 + (uint32_t)payload_len;
    msg->header.client_id   = 0x0000;
    msg->header.session_id  = ++session;
    msg->header.proto_ver   = SOMEIP_PROTO_VER;
    msg->header.iface_ver   = SOMEIP_IFACE_VER;
    msg->header.msg_type    = SOMEIP_MSG_NOTIFICATION;
    msg->header.return_code = SOMEIP_RET_OK;

    if (payload && payload_len > 0) {
        memcpy(msg->payload, payload, payload_len);
        msg->payload_len = payload_len;
    }
}

void someip_close_socket(int sock) {
    close(sock);
}
