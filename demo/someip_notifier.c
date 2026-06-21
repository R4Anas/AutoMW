#include "someip.h"
#include "someip_sd.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <time.h>

/*
 * VehicleSpeed event notifier — the "publisher" side of SOME/IP events.
 *
 * Three-phase operation:
 *
 *  Phase 1 — OfferService (SD multicast)
 *    Tell all ECUs on the network: "I have VehicleSpeed events available."
 *
 *  Phase 2 — SubscribeEventgroup (SD receive loop)
 *    When a subscriber sends Subscribe, record its IP:port and reply with
 *    SubscribeAck so it knows we accepted.
 *
 *  Phase 3 — Periodic NOTIFICATION (unicast to each subscriber)
 *    Every second, push a speed update directly to each subscriber's
 *    event socket. No polling — we push; they just listen.
 *
 * AUTOSAR parallel:
 *  Classic: sender/receiver port, event triggered by RTE from a runnable.
 *  Adaptive: ara::com event.Send() inside a SkeletonEvent.
 *  Here:    someip_build_notification() + someip_send() unicast to subscriber.
 *
 * Start this before someip_event_sub.
 */

#define MAX_SUBSCRIBERS  4
#define NOTIFY_COUNT     5

int main(void) {
    uint32_t my_ip;
    inet_pton(AF_INET, "127.0.0.1", &my_ip);

    /* SD socket — multicast group 224.224.224.245:30490 */
    int sd_sock = someip_sd_create_socket();
    if (sd_sock < 0) return 1;

    /* SOME/IP socket — sends NOTIFICATION datagrams to subscribers */
    int sock = someip_create_socket(SOMEIP_SERVER_PORT);
    if (sock < 0) return 1;

    /* Phase 1: announce service on multicast */
    someip_sd_send_offer(sd_sock, AUTOMW_SERVICE_VEHICLE, my_ip, SOMEIP_SERVER_PORT);
    printf("[NOTIFIER] VehicleSpeed event service up. Waiting for subscribers...\n\n");

    /* Phase 2+3: interleaved — watch SD socket and send notifications on a timer */
    char     sub_ips[MAX_SUBSCRIBERS][INET_ADDRSTRLEN];
    uint16_t sub_ports[MAX_SUBSCRIBERS];
    int      sub_count   = 0;
    int      notify_count = 0;
    uint32_t speed_kph   = 50;
    time_t   last_notify = time(NULL);

    while (notify_count < NOTIFY_COUNT) {
        /*
         * select() with 1-second timeout — drives both phases simultaneously.
         * If a Subscribe arrives we process it immediately.
         * If the timer expires we send the next notification.
         */
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sd_sock, &rfds);
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };

        select(sd_sock + 1, &rfds, NULL, NULL, &tv);

        /* Phase 2: handle incoming SD messages */
        if (FD_ISSET(sd_sock, &rfds)) {
            uint8_t  sd_type       = 0;
            uint16_t sd_service_id = 0;
            uint32_t sub_ip_bin    = 0;
            uint16_t sub_port      = 0;

            if (someip_sd_receive(sd_sock, &sd_type, &sd_service_id,
                                  &sub_ip_bin, &sub_port) == 0) {
                if (sd_type == SOMEIP_SD_ENTRY_SUBSCRIBE &&
                    sd_service_id == AUTOMW_SERVICE_VEHICLE &&
                    sub_count < MAX_SUBSCRIBERS) {

                    /* sub_ip_bin comes from the IPv4 endpoint option */
                    char sub_ip_str[INET_ADDRSTRLEN] = "127.0.0.1";
                    if (sub_ip_bin)
                        inet_ntop(AF_INET, &sub_ip_bin, sub_ip_str, sizeof(sub_ip_str));

                    memcpy(sub_ips[sub_count], sub_ip_str, INET_ADDRSTRLEN);
                    sub_ports[sub_count] = sub_port;
                    printf("[NOTIFIER] Subscriber %d → %s:%u\n",
                           sub_count + 1, sub_ip_str, sub_port);

                    /* Confirm subscription — unicast ack back to subscriber */
                    someip_sd_send_subscribe_ack(sd_sock, AUTOMW_SERVICE_VEHICLE,
                                                 sub_ip_str);
                    sub_count++;
                }
                /* ignore: own OfferService echo, Subscribe echoes, SubscribeAck */
            }
        }

        /* Phase 3: push notification every ~1 second if we have subscribers */
        time_t now = time(NULL);
        if (sub_count > 0 && (now - last_notify) >= 1) {
            speed_kph += 10;
            uint32_t val = speed_kph;

            someip_msg_t notif;
            someip_build_notification(&notif,
                                      AUTOMW_SERVICE_VEHICLE,
                                      AUTOMW_EVENT_SPEED_NOTIFY,
                                      &val, sizeof(val));

            for (int i = 0; i < sub_count; i++) {
                /*
                 * Unicast directly to each subscriber's event port.
                 * This is NOT multicast — the server knows exactly who
                 * subscribed and sends to each endpoint individually.
                 */
                someip_send(sock, sub_ips[i], sub_ports[i], &notif);
                printf("[NOTIFIER] NOTIFY  session=%-4u  speed=%u km/h  → %s:%u\n",
                       notif.header.session_id, speed_kph,
                       sub_ips[i], sub_ports[i]);
            }

            last_notify = now;
            notify_count++;
        }
    }

    printf("\n[NOTIFIER] Sent %d notifications. Done.\n", NOTIFY_COUNT);
    someip_close_socket(sock);
    someip_close_socket(sd_sock);
    return 0;
}
