#include "ef_lib.h"
#include "pthread.h"
#include "stdlib.h"

// Enumeration specifying the most reliable / efficient communication channel type
// between two execution flows
enum ef_communication_channel_t {
    CHANNEL_ZMQ_PIPE,
    CHANNEL_ZMQ_SOCKET,
    CHANNEL_ZMQ_PAIR,
    // Add more channels
};

// Structure representing a link between two execution flows
struct _ef_link_t {
    struct _ef_t *ef; // Pointer to the execution flow
    enum ef_communication_channel_t channel_type; // The type of communication channel between the two execution flows
    struct _ef_link_t *next; // Pointer to the next element in the linked list
};

// Structure representing an execution flow
struct _ef_t {
    void (*routine)(struct ef_t *); // Pointer to the routine that the execution flow will execute
    enum ef_abstraction_t abstraction; // The abstraction of the execution flow
    void (*cleanup)(); // Pointer to the cleanup function that will be called when the execution flow is about to be destroyed
    struct _ef_link_t *links; // Pointer to the linked list of links between this execution flows

    void *data; // Pointer to the data that the execution flow will use
    void (*start)(); // Pointer to the function that will start the execution flow
    void (*wait)(); // Pointer to the function that will wait for the execution flow to finish
};

ef_t *ef_init() {
    ef_t *ef = malloc(sizeof(ef_t));
    return ef;
}

void ef_cleanup() {

}

void *thread_start_helper(void *data) {
    ef_t *ef = (ef_t *) data;
    ef->routine(ef);
    return NULL;
}

// Starting function for an execution flow that is a thread
void ef_start_thread(ef_t *ef) {
    pthread_t *thread = (pthread_t *) ef->data;
    pthread_create(&thread, NULL, thread_start_helper, ef);
}

// Waiting function for an execution flow that is a thread
void ef_wait_thread(ef_t *ef) {
    pthread_t *thread = (pthread_t *) ef->data;
    pthread_join(thread, NULL);
}

// Starting function for an execution flow that is a process
void ef_start_process(ef_t *ef) {
    ef->data = fork();

    // If the data is 0, then we are in the child process
    if(ef->data == 0) {

        // Create a new main execution flow for a routine of the process
        // Because it's the same as enter the main() function of a program
        // Or mb we don't need to do this...
        ef_t *process_main_ef = ef_init(); // Or somehow else

        ef->routine(process_main_ef);
        exit(EXIT_SUCCESS);
    }
}

// Waiting function for an execution flow that is a process
void ef_wait_process(ef_t *ef) {
    waitpid(ef->data, NULL, 0);
}

ef_t *ef_create(void (*start_routine)(ef_t *), enum ef_abstraction_t abstraction, void (*cleanup)()) {
    
    ef_t *ef = malloc(sizeof(ef_t));
    ef->routine = start_routine;
    ef->abstraction = abstraction;
    ef->cleanup = cleanup;
    ef->links = NULL;

    // TODO: Determine the most efficient abstraction for the execution flow if specied more than one

    if (abstraction & EF_THREAD) {
        ef->start = ef_start_thread;
        ef->wait = ef_wait_thread;
    } else if (abstraction & EF_PROCESS) {
        ef->start = ef_start_process;
        ef->wait = ef_wait_process;
    } else {
        printf("Invalid abstraction specified\n");
        exit(EXIT_FAILURE);
    }

    return ef;
}

void ef_link(ef_t *ef1, ef_t *ef2) {
    // TODO: 

    // Determine the most reliable / efficient zmq communication channel type between the two execution flows
    // based on their abstractions

    // Create a new link between the two execution flows
}

void ef_start(ef_t *ef) {
    ef->start(ef);
}

void ef_wait(ef_t *ef) {
    ef->wait(ef);
}