#include "automw_shm.h"
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

/*
 * Simulates an ECU publishing two signals periodically.
 * Run this first — it creates the bus.
 * Then run subscriber in another terminal.
 */
int main(void) {
    if (automw_bus_create("vehicle") < 0) {
        fprintf(stderr, "Failed to create bus\n");
        return 1;
    }

    uint32_t speed = 0;
    uint32_t rpm   = 800;

    printf("Publishing on 'vehicle' bus...\n");

    for (int i = 0; i < 20; i++) {
        automw_publish("vehicle", "VehicleSpeed", &speed, sizeof(speed));
        automw_publish("vehicle", "EngineRPM",    &rpm,   sizeof(rpm));

        printf("[PUB] VehicleSpeed=%u km/h  EngineRPM=%u\n", speed, rpm);

        speed = (speed + 10) % 200;
        rpm   = 800 + (speed * 30);

        sleep(1);
    }

    automw_bus_destroy("vehicle");
    return 0;
}