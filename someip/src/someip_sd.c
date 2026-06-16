#include "someip_sd.h"
#include "someip.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

/*
 * SOME/IP-SD uses UDP multicast on 224.224.224.245:30490
 *
 * Multicast basics:
 * Unlike unicast (one sender, one receiver) or broadcast (one sender,
 * all receivers), multicast sends to a group — only processes that
 * explicitly join the group receive the packets.
 *
 * 224.x.x.x is the IPv4 multicast range (Class D addresses)
 * 224.224.224.245 is the SOME/IP-SD standard multicast address
 *
 * All ECUs join this group at startup — like tuning to a radio frequency.
 * When server sends OfferService to this address, all ECUs receive it
 * without the server knowing their individual IP addresses.
 *
 * AUTOSAR parallel:
 * In Classic AUTOSAR, service availability is in the ARXML — static.
 * SOME/IP-SD makes it dynamic — ECUs discover each other at runtime.
 * This is the fundamental shift from Classic to Adaptive AUTOSAR.
 */

/*
 * someip_sd_create_socket()
 *
 * Creates a UDP socket for SOME/IP-SD multicast.
 * Three key socket options:
 *
 * SO_REUSEADDR — multiple processes can bind to same port
 *               needed because both server and client bind to 30490
 *
 * IP_ADD_MEMBERSHIP — joins the multicast group
 *               without this, multicast packets are dropped by kernel
 *               even if they arrive on the network interface
 *
 * IP_MULTICAST_LOOP — receive your own multicast packets on loopback
 *               needed for our demo where server and client are on
 *               the same machine. On real ECUs this would be 0.
 */
int someip_sd_create_socket(void) {
    /* [POSIX API] Create UDP socket */
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        fprintf(stderr, "[someip_sd] socket() failed: %s\n", strerror(errno));
        return -1;
    }

    /* [POSIX API] Allow multiple sockets on same port */
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    /* [POSIX API] Bind to SD port */
    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(SOMEIP_SD_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[someip_sd] bind() failed: %s\n", strerror(errno));
        close(sock);
        return -1;
    }

    /*
     * [POSIX API] Join multicast group
     * ip_mreq tells kernel: join group 224.224.224.245
     * on the default network interface (INADDR_ANY)
     */
    struct ip_mreq mreq = {0};
    inet_pton(AF_INET, SOMEIP_SD_MULTICAST, &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = INADDR_ANY;

    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   &mreq, sizeof(mreq)) < 0) {
        fprintf(stderr, "[someip_sd] IP_ADD_MEMBERSHIP failed: %s\n",
                strerror(errno));
        close(sock);
        return -1;
    }

    /*
     * [POSIX API] Enable multicast loopback
     * Allows this machine to receive its own multicast packets
     * Required when server and client run on the same machine
     */
    uint8_t loop = 1;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

    printf("[someip_sd] SD socket ready on %s:%d\n",
           SOMEIP_SD_MULTICAST, SOMEIP_SD_PORT);
    return sock;
}

/*
 * someip_sd_send_offer()
 *
 * Server announces: "I provide service X at IP:port"
 * Sent to multicast group so all ECUs receive it.
 *
 * SD message structure:
 * SOME/IP header (service=0xFFFF, method=0x8100)
 *   └── SD header (flags)
 *       └── entries array (one OFFER entry)
 *           └── options array (one IPv4 endpoint option)
 */
int someip_sd_send_offer(int sock,
                          uint16_t service_id,
                          uint32_t server_ip,
                          uint16_t server_port)
{
    /*
     * SD payload layout:
     * [flags 1B][reserved 3B]
     * [entries length 4B][entry 16B]
     * [options length 4B][option 12B]
     */
    uint8_t payload[64];
    memset(payload, 0, sizeof(payload));
    size_t offset = 0;

    /* SD flags — reboot flag set on first offer after startup */
    payload[offset++] = SOMEIP_SD_FLAG_REBOOT | SOMEIP_SD_FLAG_UNICAST;
    payload[offset++] = 0x00; /* reserved */
    payload[offset++] = 0x00;
    payload[offset++] = 0x00;

    /* Entries array length = 16 bytes (one entry) */
    uint32_t entries_len = htonl(sizeof(someip_sd_entry_t));
    memcpy(payload + offset, &entries_len, 4); offset += 4;

    /* Build OFFER entry */
    someip_sd_entry_t entry = {0};
    entry.type         = SOMEIP_SD_ENTRY_OFFER;
    entry.index_first  = 0x00;
    entry.index_second = 0x00;
    entry.num_options  = 0x01;  /* one IPv4 option follows */
    entry.service_id   = htons(service_id);
    entry.instance_id  = htons(0x0001);
    entry.major_ver    = 0x01;
    /* TTL = 0xFFFFFF = offer valid forever */
    entry.ttl[0] = 0xFF;
    entry.ttl[1] = 0xFF;
    entry.ttl[2] = 0xFF;
    entry.minor_ver = htonl(0x00000001);

    memcpy(payload + offset, &entry, sizeof(entry));
    offset += sizeof(entry);

    /* Options array length = 12 bytes (one IPv4 option) */
    uint32_t options_len = htonl(sizeof(someip_sd_option_ipv4_t));
    memcpy(payload + offset, &options_len, 4); offset += 4;

    /* Build IPv4 endpoint option — tells client where server is */
    someip_sd_option_ipv4_t opt = {0};
    opt.length     = htons(0x0009);
    opt.type       = 0x04;  /* IPv4 endpoint */
    opt.reserved   = 0x00;
    opt.ip_address = server_ip;  /* already in network byte order */
    opt.reserved2  = 0x00;
    opt.protocol   = SOMEIP_SD_PROTO_UDP;
    opt.port       = htons(server_port);

    memcpy(payload + offset, &opt, sizeof(opt));
    offset += sizeof(opt);

    /* Build SOME/IP wrapper for SD message */
    someip_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.service_id  = SOMEIP_SD_SERVICE_ID;   /* 0xFFFF */
    msg.header.method_id   = SOMEIP_SD_METHOD_ID;    /* 0x8100 */
    msg.header.length      = 8 + offset;
    msg.header.client_id   = SOMEIP_SD_CLIENT_ID;
    msg.header.session_id  = 0x0001;
    msg.header.proto_ver   = SOMEIP_PROTO_VER;
    msg.header.iface_ver   = SOMEIP_IFACE_VER;
    msg.header.msg_type    = SOMEIP_MSG_NOTIFICATION;
    msg.header.return_code = SOMEIP_RET_OK;
    memcpy(msg.payload, payload, offset);
    msg.payload_len = offset;

    printf("[someip_sd] sending OfferService 0x%04X at port %u\n",
           service_id, server_port);

    return someip_send(sock, SOMEIP_SD_MULTICAST,
                       SOMEIP_SD_PORT, &msg);
}

/*
 * someip_sd_send_find()
 *
 * Client broadcasts: "Is anyone offering service X?"
 * Sent to multicast group — any server providing that service will reply
 * with OfferService (someip_sd_send_offer).
 *
 * FindService has no endpoint option — client doesn't advertise its address,
 * it just asks a question. Server sees the multicast and replies.
 * The reply goes back to the multicast group so all nodes can learn.
 */
int someip_sd_send_find(int sock, uint16_t service_id) {
    uint8_t payload[40];
    memset(payload, 0, sizeof(payload));
    size_t offset = 0;

    /* SD flags */
    payload[offset++] = SOMEIP_SD_FLAG_REBOOT | SOMEIP_SD_FLAG_UNICAST;
    payload[offset++] = 0x00; /* reserved */
    payload[offset++] = 0x00;
    payload[offset++] = 0x00;

    /* Entries array length = 16 bytes (one FIND entry) */
    uint32_t entries_len = htonl(sizeof(someip_sd_entry_t));
    memcpy(payload + offset, &entries_len, 4); offset += 4;

    /* FIND entry — no options (we're asking, not advertising an endpoint) */
    someip_sd_entry_t entry = {0};
    entry.type         = SOMEIP_SD_ENTRY_FIND;
    entry.index_first  = 0x00;
    entry.index_second = 0x00;
    entry.num_options  = 0x00;
    entry.service_id   = htons(service_id);
    entry.instance_id  = htons(SOMEIP_SD_INSTANCE_ANY); /* accept any instance */
    entry.major_ver    = 0xFF;                           /* accept any version  */
    entry.ttl[0] = 0x00;
    entry.ttl[1] = 0x00;
    entry.ttl[2] = 0x03;                 /* TTL = 3 seconds for this search    */
    entry.minor_ver = htonl(0xFFFFFFFF); /* accept any minor version           */

    memcpy(payload + offset, &entry, sizeof(entry));
    offset += sizeof(entry);

    /* Options array length = 0 (no options) */
    uint32_t options_len = htonl(0);
    memcpy(payload + offset, &options_len, 4); offset += 4;

    someip_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.service_id  = SOMEIP_SD_SERVICE_ID;
    msg.header.method_id   = SOMEIP_SD_METHOD_ID;
    msg.header.length      = 8 + offset;
    msg.header.client_id   = SOMEIP_SD_CLIENT_ID;
    msg.header.session_id  = 0x0001;
    msg.header.proto_ver   = SOMEIP_PROTO_VER;
    msg.header.iface_ver   = SOMEIP_IFACE_VER;
    msg.header.msg_type    = SOMEIP_MSG_NOTIFICATION;
    msg.header.return_code = SOMEIP_RET_OK;
    memcpy(msg.payload, payload, offset);
    msg.payload_len = offset;

    printf("[someip_sd] sending FindService 0x%04X\n", service_id);

    return someip_send(sock, SOMEIP_SD_MULTICAST, SOMEIP_SD_PORT, &msg);
}

/*
 * someip_sd_receive()
 *
 * Receives and parses one SD message.
 * Extracts entry type, service ID, and server IP/port from options.
 */
int someip_sd_receive(int sock,
                       uint8_t *out_type,
                       uint16_t *out_service_id,
                       uint32_t *out_ip,
                       uint16_t *out_port)
{
    someip_msg_t msg;
    char src_ip[INET_ADDRSTRLEN];
    uint16_t src_port;

    if (someip_receive(sock, &msg, src_ip, &src_port) < 0) return -1;

    /* Verify this is an SD message */
    if (msg.header.service_id != SOMEIP_SD_SERVICE_ID) return -1;

    uint8_t *p = msg.payload;
    size_t   len = msg.payload_len;

    if (len < 8) return -1;  /* need at least flags + entries_len */

    /* Skip flags (4 bytes) */
    p += 4; len -= 4;

    /* Read entries length */
    uint32_t entries_len = ntohl(*(uint32_t *)p);
    p += 4; len -= 4;

    if (len < entries_len) return -1;

    /* Parse first entry */
    someip_sd_entry_t *entry = (someip_sd_entry_t *)p;
    if (out_type)       *out_type       = entry->type;
    if (out_service_id) *out_service_id = ntohs(entry->service_id);

    p   += entries_len;
    len -= entries_len;

    /* Parse options for IP and port */
    if (len >= 4) {
        uint32_t options_len = ntohl(*(uint32_t *)p);
        p += 4; len -= 4;

        if (len >= options_len && options_len >= sizeof(someip_sd_option_ipv4_t)) {
            someip_sd_option_ipv4_t *opt = (someip_sd_option_ipv4_t *)p;
            if (out_ip)   *out_ip   = opt->ip_address;
            if (out_port) *out_port = ntohs(opt->port);
        }
    }

    return 0;
}
