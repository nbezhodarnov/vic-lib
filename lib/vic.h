#ifndef VIC_LIB_H
#define VIC_LIB_H

// Forward declaration of the virtual isolation context structure
typedef struct _vic_t vic_t;

// Forward declaration of the execution flow structure
typedef struct _vic_ef_t vic_ef_t;

enum vic_abstraction_t {
    EF_THREAD = 0x01,
    EF_PROCESS = 0x02
};

// Initialize the ef library and return a pointer to the root execution flow
vic_t *vic_init();

vic_t *vic_create(enum vic_abstraction_t abstraction);

// Create a new child execution flow
vic_ef_t *vic_ef_create(vic_t *vic, void (*start_routine)(vic_t *), void (*finished)(vic_t *));

vic_ef_t *vic_ef_get(vic_t *vic);

void vic_destroy(vic_t *vic);

void vic_ef_destroy(vic_ef_t *ef);

int vic_ef_send(vic_ef_t *ef, const char* name, const char* data);

char* vic_ef_recv(vic_ef_t *ef, const char* name);

// Link two execution flows together
void vic_link(vic_t *vic1, vic_t *vic2, const char *name);

// Start an execution flow
void vic_ef_start(vic_ef_t *ef);

// Wait for an execution flow to finish
void vic_ef_wait(vic_ef_t *ef);



#endif // VIC_LIB_H