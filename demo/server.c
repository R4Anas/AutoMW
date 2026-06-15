
#include "automw_mq.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/*
 * A "VehicleState" service — owns the authoritative vehicle state
 * and responds to client requests.
 *
 * Request payload: 1 byte operation code
 *   0x01 = GetSpeed
 *   0x02 = GetOdometer
 *
 * Response payload: 4 byte uint32_t value
 *
 * Maps to AUTOSAR DCM: server owns the data,
 * clients request it via defined service interface.
 */
int main(void) {
    if (automw_server_create("VehicleState") < 0) {
        fprintf(stderr, "Failed to create service\n");
        return 1;
    }

    uint32_t speed_kph  = 120;
    uint32_t odometer_m = 54321;

    printf("[SERVER] VehicleState service running...\n");

    for (int i = 0; i < 10; i++) {
        automw_request_t req;
        memset(&req, 0, sizeof(req));

        printf("[SERVER] Waiting for request...\n");
        if (automw_server_receive("VehicleState", &req) < 0) break;

        uint8_t op = req.payload[0];
        printf("[SERVER] Got request_id=%u op=0x%02X\n",
               req.request_id, op);

        automw_response_t resp;
        memset(&resp, 0, sizeof(resp));
        resp.request_id = req.request_id;
        resp.status     = 0;

        uint32_t value = 0;
        if (op == 0x01)      value = speed_kph;
        else if (op == 0x02) value = odometer_m;
        else                 resp.status = -1;

        memcpy(resp.payload, &value, sizeof(value));
        resp.payload_len = sizeof(value);

        automw_server_reply("VehicleState", &resp);
        printf("[SERVER] Replied with value=%u\n", value);
    }

    automw_server_destroy("VehicleState");
    return 0;
}