#include "automw_shm.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/*
 * POSIX shared memory lives in /dev/shm on Linux.
 * The name must start with '/'. We prefix bus names automatically.
 *
 * We also create a named semaphore for mutual exclusion.
 * Without it, two publishers writing simultaneously corrupt the bus.
 * This maps to AUTOSAR's concept of resource protection via
 * GetResource/ReleaseResource in the OS.
 */

#define SHM_PREFIX "/automw_"
#define SEM_PREFIX "/automw_sem_"
#define MAX_BUS_PATH 64

static void make_shm_name(char *out, const char *bus_name) {
    snprintf(out, MAX_BUS_PATH, "%s%s", SHM_PREFIX, bus_name);
}

static void make_sem_name(char *out, const char *bus_name) {
    snprintf(out, MAX_BUS_PATH, "%s%s", SEM_PREFIX, bus_name);
}

/*
 * automw_bus_create()
 *
 * Creates the shared memory region and initializes the bus struct.
 * Only the first process (publisher) calls this.
 *
 * shm_open() with O_CREAT | O_EXCL fails if it already exists —
 * same as open() with O_EXCL. This prevents two publishers
 * accidentally creating two buses with the same name.
 *
 * ftruncate() sets the size — shared memory starts at 0 bytes,
 * you must explicitly size it before mapping.
 *
 * mmap() maps the physical shared memory pages into THIS process's
 * virtual address space. MAP_SHARED means writes are visible to
 * other processes that also map the same object.
 * Compare to MAP_PRIVATE which gives you a copy-on-write mapping
 * only visible to your process — useful for read-only file access.
 */
int automw_bus_create(const char *bus_name) {
    char shm_name[MAX_BUS_PATH];
    char sem_name[MAX_BUS_PATH];
    make_shm_name(shm_name, bus_name);
    make_sem_name(sem_name, bus_name);

    /* Create shared memory object */
    int fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0666);
    if (fd < 0) {
        fprintf(stderr, "[automw] bus_create failed: %s\n", strerror(errno));
        return -1;
    }

    /* Size it to hold our bus struct */
    if (ftruncate(fd, sizeof(automw_bus_t)) < 0) {
        fprintf(stderr, "[automw] ftruncate failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    /* Map into our address space */
    automw_bus_t *bus = mmap(NULL, sizeof(automw_bus_t),
                              PROT_READ | PROT_WRITE,
                              MAP_SHARED, fd, 0);
    close(fd); /* fd no longer needed after mmap */

    if (bus == MAP_FAILED) {
        fprintf(stderr, "[automw] mmap failed: %s\n", strerror(errno));
        return -1;
    }

    /* Zero-initialize the bus */
    memset(bus, 0, sizeof(automw_bus_t));
    munmap(bus, sizeof(automw_bus_t));

    /*
     * Create the semaphore with initial value 1 (binary semaphore / mutex).
     * sem_open() creates a named semaphore visible across processes —
     * unlike sem_init() which is per-process only.
     * Initial value 1 means "available". sem_wait() decrements to 0
     * (locked), sem_post() increments back to 1 (unlocked).
     */
    sem_t *sem = sem_open(sem_name, O_CREAT | O_EXCL, 0666, 1);
    if (sem == SEM_FAILED) {
        fprintf(stderr, "[automw] sem_open failed: %s\n", strerror(errno));
        shm_unlink(shm_name);
        return -1;
    }
    sem_close(sem);

    printf("[automw] bus '%s' created\n", bus_name);
    return 0;
}

/*
 * automw_bus_open()
 *
 * Called by subscribers to attach to an existing bus.
 * O_RDONLY — subscribers only read. Enforced at mmap level too.
 */
int automw_bus_open(const char *bus_name) {
    char shm_name[MAX_BUS_PATH];
    make_shm_name(shm_name, bus_name);

    int fd = shm_open(shm_name, O_RDONLY, 0);
    if (fd < 0) {
        fprintf(stderr, "[automw] bus_open failed: %s\n", strerror(errno));
        return -1;
    }
    close(fd);
    return 0;
}

/*
 * automw_publish()
 *
 * Finds or creates a signal slot on the bus and writes data.
 *
 * Critical section is protected by the named semaphore.
 * sem_wait() is a blocking call — if another publisher holds
 * the semaphore, this call blocks until it's released.
 * In AUTOSAR terms this is equivalent to GetResource().
 *
 * The counter increment is how subscribers detect new data —
 * they cache the last counter value they saw and compare.
 */
int automw_publish(const char *bus_name,
                   const char *signal_name,
                   const void *data,
                   size_t      data_len)
{
    if (data_len > 64) {
        fprintf(stderr, "[automw] data_len exceeds max (64)\n");
        return -1;
    }

    char shm_name[MAX_BUS_PATH];
    char sem_name[MAX_BUS_PATH];
    make_shm_name(shm_name, bus_name);
    make_sem_name(sem_name, bus_name);

    int fd = shm_open(shm_name, O_RDWR, 0);
    if (fd < 0) return -1;

    automw_bus_t *bus = mmap(NULL, sizeof(automw_bus_t),
                              PROT_READ | PROT_WRITE,
                              MAP_SHARED, fd, 0);
    close(fd);
    if (bus == MAP_FAILED) return -1;

    sem_t *sem = sem_open(sem_name, 0);
    if (sem == SEM_FAILED) {
        munmap(bus, sizeof(automw_bus_t));
        return -1;
    }

    /* Enter critical section */
    sem_wait(sem);

    /* Find existing slot or allocate new one */
    automw_signal_t *slot = NULL;
    for (uint32_t i = 0; i < bus->signal_count; i++) {
        if (strncmp(bus->signals[i].name, signal_name,
                    AUTOMW_MAX_NAME_LEN) == 0) {
            slot = &bus->signals[i];
            break;
        }
    }

    if (!slot) {
        if (bus->signal_count >= AUTOMW_MAX_SIGNALS) {
            fprintf(stderr, "[automw] bus full\n");
            sem_post(sem);
            sem_close(sem);
            munmap(bus, sizeof(automw_bus_t));
            return -1;
        }
        slot = &bus->signals[bus->signal_count++];
        strncpy(slot->name, signal_name, AUTOMW_MAX_NAME_LEN - 1);
    }

    memcpy(slot->data, data, data_len);
    slot->data_len = data_len;
    slot->counter++;  /* Subscribers detect update via this */

    /* Exit critical section */
    sem_post(sem);

    sem_close(sem);
    munmap(bus, sizeof(automw_bus_t));
    return 0;
}

/*
 * automw_subscribe()
 *
 * Reads current value of a named signal.
 * No blocking — returns whatever is currently on the bus.
 * Caller should poll periodically or track counter for changes.
 *
 * No semaphore here intentionally for read — on x86/ARM,
 * reading a cache-line-aligned struct is effectively atomic.
 * For production you'd use a seqlock here. Worth knowing about:
 * a seqlock lets multiple readers proceed without blocking,
 * while writers increment a sequence counter before/after write.
 * Readers detect a concurrent write if the counter is odd or changed.
 */
int automw_subscribe(const char *bus_name,
                     const char *signal_name,
                     void       *out_data,
                     size_t     *out_len)
{
    char shm_name[MAX_BUS_PATH];
    make_shm_name(shm_name, bus_name);

    int fd = shm_open(shm_name, O_RDONLY, 0);
    if (fd < 0) return -1;

    automw_bus_t *bus = mmap(NULL, sizeof(automw_bus_t),
                              PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (bus == MAP_FAILED) return -1;

    int found = -1;
    for (uint32_t i = 0; i < bus->signal_count; i++) {
        if (strncmp(bus->signals[i].name, signal_name,
                    AUTOMW_MAX_NAME_LEN) == 0) {
            memcpy(out_data, bus->signals[i].data,
                   bus->signals[i].data_len);
            *out_len = bus->signals[i].data_len;
            found = 0;
            break;
        }
    }

    munmap(bus, sizeof(automw_bus_t));
    return found;
}

void automw_bus_close(const char *bus_name) {
    /* Nothing to do — mmap is unmapped after each call */
    (void)bus_name;
}

/*
 * automw_bus_destroy()
 *
 * Removes shared memory and semaphore from the system.
 * shm_unlink() removes the name from /dev/shm — but existing
 * mappings in other processes remain valid until they munmap().
 * This is reference-counted by the kernel — same as unlink() on files.
 */
void automw_bus_destroy(const char *bus_name) {
    char shm_name[MAX_BUS_PATH];
    char sem_name[MAX_BUS_PATH];
    make_shm_name(shm_name, bus_name);
    make_sem_name(sem_name, bus_name);

    shm_unlink(shm_name);
    sem_unlink(sem_name);
    printf("[automw] bus '%s' destroyed\n", bus_name);
}