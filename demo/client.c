#include "automw_mq.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

int main(void) {
    printf("[CLIENT] Calling VehicleState service...\n");

    /* Request 1: GetSpeed */
    automw_request_t req;
    
    memset(&req, 0, sizeof(req));
    req.request_id  = 1001;
    req.payload[0]  = 0x01; /* GetSpeed */
    req.payload_len = 1;

    automw_response_t resp;
    if (automw_client_call("VehicleState", &req, &resp, 2000) == 0) {
        uint32_t speed;
        memcpy(&speed, resp.payload, sizeof(speed));
        printf("[CLIENT] GetSpeed → %u km/h (status=%d)\n",
               speed, resp.status);
    } else {
        printf("[CLIENT] GetSpeed call failed/timeout\n");
    }

    /* Request 2: GetOdometer */
    memset(&req, 0, sizeof(req));
    req.request_id  = 1002;
    req.payload[0]  = 0x02; /* GetOdometer */
    req.payload_len = 1;

    if (automw_client_call("VehicleState", &req, &resp, 2000) == 0) {
        uint32_t odo;
        memcpy(&odo, resp.payload, sizeof(odo));
        printf("[CLIENT] GetOdometer → %u m (status=%d)\n",
               odo, resp.status);
    } else {
        printf("[CLIENT] GetOdometer call failed/timeout\n");
    }

    return 0;
}