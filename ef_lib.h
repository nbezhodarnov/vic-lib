#ifndef EF_LIB_H
#define EF_LIB_H

typedef struct _vic_t vic_t;

// Forward declaration of the execution flow structure
// making it opaque to the user
typedef struct _vic_ef_t vic_ef_t;

enum vic_abstraction_t {
    EF_THREAD = 0x01,
    EF_PROCESS = 0x02
};

// Initialize the ef library and return a pointer to the root execution flow
vic_t *vic_init();

vic_t *vic_create(enum vic_abstraction_t abstraction);

// Create a new child execution flow
vic_ef_t *vic_ef_create(vic_t *vic, void (*start_routine)(vic_ef_t *), void (*finished)(vic_ef_t *));

void vic_destroy(vic_t *vic);

void vic_ef_destroy(vic_ef_t *ef);

int vic_ef_send(vic_ef_t *ef, const char* name, const char* data);

char* vic_ef_recv(vic_ef_t *ef, const char* name);

// Link two execution flows together
void vic_ef_link(vic_ef_t *ef1, vic_ef_t *ef2, const char *name);

// Start an execution flow
void vic_ef_start(vic_ef_t *ef);

// Wait for an execution flow to finish
void vic_ef_wait(vic_ef_t *ef);



#endif //EF_LIB_H