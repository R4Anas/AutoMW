#ifndef AUTOMW_SHM_H
#define AUTOMW_SHM_H

#include <stdint.h>
#include <stddef.h>

/* Max signals on the bus — like AUTOSAR COM signal count */
#define AUTOMW_MAX_SIGNALS  32
#define AUTOMW_MAX_NAME_LEN 32

/* A single signal on the shared bus — mirrors AUTOSAR COM signal concept */
typedef struct {
    char     name[AUTOMW_MAX_NAME_LEN]; /* signal name e.g. "VehicleSpeed" */
    uint8_t  data[64];                  /* raw payload */
    size_t   data_len;                  /* actual payload size */
    uint32_t counter;                   /* increments on every publish */
} automw_signal_t;

/* The shared bus — lives in shared memory, all processes see this */
typedef struct {
    automw_signal_t signals[AUTOMW_MAX_SIGNALS];
    uint32_t        signal_count;
} automw_bus_t;

/* Publisher side */
int  automw_bus_create(const char *bus_name);   /* creates shared memory */
int  automw_publish(const char *bus_name,
                    const char *signal_name,
                    const void *data,
                    size_t      data_len);

/* Subscriber side */
int  automw_bus_open(const char *bus_name);     /* opens existing shared memory */
int  automw_subscribe(const char *bus_name,
                      const char *signal_name,
                      void       *out_data,
                      size_t     *out_len);

/* Cleanup */
void automw_bus_close(const char *bus_name);
void automw_bus_destroy(const char *bus_name);

#endif /* AUTOMW_SHM_H */