#ifndef EF_LIB_H
#define EF_LIB_H

// Forward declaration of the execution flow structure
// making it opaque to the user
typedef struct _ef_t ef_t;

enum ef_abstraction_t {
    EF_THREAD = 0x01,
    EF_PROCESS = 0x02
};

// Initialize the ef library and return a pointer to the root execution flow
ef_t *ef_init();

// Cleanup the ef library
void ef_cleanup();

// Create a new child execution flow
ef_t *ef_create(void (*start_routine)(ef_t *), enum ef_abstraction_t abstraction, void (*finished)(ef_t *));

void ef_destroy(ef_t *ef);

int ef_send(ef_t *ef, const char* name, const char* data);

char* ef_recv(ef_t *ef, const char* name);

// Link two execution flows together
void ef_link(ef_t *ef1, ef_t *ef2, const char *name);

// Start an execution flow
void ef_start(ef_t *ef);

// Wait for an execution flow to finish
void ef_wait(ef_t *ef);



#endif //EF_LIB_H