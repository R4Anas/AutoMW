#include "automw_shm.h"
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

/*
 * Polls the bus for signal updates.
 * Uses the counter field to detect new data — only prints when changed.
 * This is the same pattern as AUTOSAR COM's InvalidationAction —
 * you track whether data has been refreshed since you last read it.
 */
int main(void) {
    if (automw_bus_open("vehicle") < 0) {
        fprintf(stderr, "Bus not found — start publisher first\n");
        return 1;
    }

    uint32_t speed = 0, rpm = 0;
    size_t   len = 0;

    printf("Subscribing to 'vehicle' bus...\n");

    for (int i = 0; i < 40; i++) {
        if (automw_subscribe("vehicle", "VehicleSpeed",
                             &speed, &len) == 0) {
            printf("[SUB] VehicleSpeed = %u km/h\n", speed);
        }
        if (automw_subscribe("vehicle", "EngineRPM",
                             &rpm, &len) == 0) {
            printf("[SUB] EngineRPM    = %u\n", rpm);
        }
        usleep(500000); /* poll at 2Hz */
    }

    automw_bus_close("vehicle");
    return 0;
}