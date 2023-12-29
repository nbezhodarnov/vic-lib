#include "ef_lib.h"
#include "pthread.h"
#include "stdlib.h"

#include <string.h>
#include <czmq.h>

#define CC_NO_SHORT_NAMES
#include "cc.h"

#define ADDR_BUFFER_LEN 256

// Structure representing a link between two virtual isolation contexts
typedef struct
{
    int zmq_type; // The type of the zmq socket
    char *zmq_transport_prefix; // The transport prefix of the zmq socket
    char *zmq_addr; // The full address of the zmq socket (prefix + name)

    zsock_t *zmq_sock; // The zmq socket
    int zmq_bind; // 1 if the socket is bound, 0 if the socket is connected
} _vic_link_t;

typedef cc_list(_vic_link_t) _vic_link_list_t;

struct _vic_t
{
    enum vic_abstraction_t abstraction; // The abstraction of the virtual isolation context
    void *data;                         // Pointer to the data that the virtual isolation context will use
    vic_ef_t *ef;                       // Pointer to the root execution flow of the virtual isolation context (must be one-to-one only)

    _vic_link_list_t links; // Pointer to the linked list of links between virtual isolation contexts

    void (*start)(vic_ef_t *); // Pointer to the function that will start the execution flow
    void (*wait)(vic_ef_t *);  // Pointer to the function that will wait for the execution flow to finish

    void (*destroy)(vic_t *); // Pointer to the function that will destroy the execution flow cleaning up all the resources
};

// Structure representing an execution flow
struct _vic_ef_t
{
    vic_t *vic; // Pointer to the virtual isolation context that the execution flow belongs to

    void (*routine)(vic_ef_t *);  // Pointer to the routine that the execution flow will execute
    void (*finished)(vic_ef_t *); // Pointer to the function that will be called when the execution flow is about to be destroyed
};

vic_t *_vic_new()
{
    vic_t *vic = malloc(sizeof(vic_t));
    vic->abstraction = 0;
    vic->data = NULL;
    vic->ef = NULL;

    cc_init(&vic->links);

    vic->start = NULL;
    vic->wait = NULL;
    vic->destroy = NULL;

    return vic;
}

vic_ef_t *_ef_new()
{
    vic_ef_t *ef = malloc(sizeof(vic_ef_t));

    ef->routine = NULL;
    ef->finished = NULL;
    ef->vic = NULL;

    return ef;
}

vic_t *vic_init()
{
    vic_t *main_vic = _vic_new();
    main_vic->abstraction = EF_THREAD;

    return main_vic;
}

void vic_destroy(vic_t *vic)
{
    assert(vic->ef == NULL);

    cc_for_each(&vic->links, link)
    {
        zstr_free(&link->zmq_addr);
        zstr_free(&link->zmq_transport_prefix);

        if (link->zmq_sock)
            zsock_destroy(&link->zmq_sock);
    }

    cc_cleanup(&vic->links);

    if (vic->destroy != NULL)
        vic->destroy(vic);

    free(vic);
}

void vic_ef_destroy(vic_ef_t *ef)
{
    if (ef->finished != NULL)
        ef->finished(ef);

    ef->vic->ef = NULL;
    free(ef);
}

void *_vic_thread_start_helper(void *data)
{
    vic_ef_t *ef = (vic_ef_t *)data;
    ef->routine(ef);
    return NULL;
}

void _vic_start_helper(vic_ef_t *ef)
{
    cc_for_each(&ef->vic->links, link)
    {
        link->zmq_sock = zsock_new(link->zmq_type);

        // Disable false positive warning
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"

        if (link->zmq_bind)
        {
            zsock_bind(link->zmq_sock, link->zmq_addr);
        }
        else
        {
            zsock_connect(link->zmq_sock, link->zmq_addr);
        }

#pragma GCC diagnostic pop
    }
}

// Starting function for an execution flow that is a thread
void _vic_start_thread(vic_ef_t *ef)
{
    _vic_start_helper(ef);

    ef->vic->data = malloc(sizeof(pthread_t));
    pthread_create((pthread_t *)ef->vic->data, NULL, _vic_thread_start_helper, ef);
}

// Waiting function for an execution flow that is a thread
void _vic_wait_thread(vic_ef_t *ef)
{
    pthread_t *thread = (pthread_t *)ef->vic->data;
    pthread_join(*thread, NULL);
}

void _vic_destroy_thread(vic_t *vic)
{
    pthread_t *thread = (pthread_t *)vic->data;
    free(thread);
}

void _vic_destroy_process(vic_t *vic)
{
}

// Starting function for an execution flow that is a process
void _vic_start_process(vic_ef_t *ef)
{
    // TODO : Check for the errors of fork call

    ef->vic->data = (void *)(uintptr_t)fork();

    // If the data is 0, then we are in the child process
    if (ef->vic->data == 0)
    {
        zsys_shutdown();

        _vic_start_helper(ef);

        ef->routine(ef);

        vic_t *vic = ef->vic;
        vic_ef_destroy(ef);
        vic_destroy(vic);

        exit(EXIT_SUCCESS);
    }
}

// Waiting function for an execution flow that is a process
void _vic_wait_process(vic_ef_t *ef)
{
    // TODO: Check if the process is launched

    waitpid((pid_t)(uintptr_t)ef->vic->data, NULL, 0);
}

vic_t *vic_create(enum vic_abstraction_t abstraction)
{
    vic_t *vic = _vic_new();

    if (abstraction & EF_THREAD)
    {
        vic->abstraction = EF_THREAD;
        vic->start = _vic_start_thread;
        vic->wait = _vic_wait_thread;
        vic->destroy = _vic_destroy_thread;
    }
    else if (abstraction & EF_PROCESS)
    {
        vic->abstraction = EF_PROCESS;
        vic->start = _vic_start_process;
        vic->wait = _vic_wait_process;
        vic->destroy = _vic_destroy_process;
    }
    else
    {
        printf("Invalid abstraction specified\n");
        exit(EXIT_FAILURE);
    }

    return vic;
}

vic_ef_t *vic_ef_create(vic_t *vic, void (*start_routine)(vic_ef_t *), void (*finished)(vic_ef_t *))
{
    vic_ef_t *ef = _ef_new();
    ef->routine = start_routine;
    ef->finished = finished;

    vic->ef = ef;
    ef->vic = vic;

    return ef;
}

void _vic_link_init_helper(_vic_link_t *link)
{
    link->zmq_type = 0;
    link->zmq_transport_prefix = NULL;
    link->zmq_addr = NULL;
    link->zmq_sock = NULL;
    link->zmq_bind = 0;
}

void _vic_link_helper(vic_t *vic1, vic_t *vic2, const char *name, int zmq_type, const char *transport_prefix)
{
    _vic_link_t vic1_link;
    _vic_link_init_helper(&vic1_link);
    _vic_link_t vic2_link;
    _vic_link_init_helper(&vic2_link);

    vic1_link.zmq_bind = 0;
    vic2_link.zmq_bind = 1;

    vic1_link.zmq_type = zmq_type;
    vic1_link.zmq_transport_prefix = strdup(transport_prefix);

    vic2_link.zmq_type = zmq_type;
    vic2_link.zmq_transport_prefix = strdup(transport_prefix);

    int total_len = strlen(transport_prefix) + strlen(name) + 1;
    char *addr = (char *)calloc(total_len, sizeof(char));

    strcpy(addr, transport_prefix);
    strcat(addr, name);

    vic1_link.zmq_addr = addr;
    vic2_link.zmq_addr = strdup(addr);

    cc_push(&vic1->links, vic1_link);
    cc_push(&vic2->links, vic2_link);
}

void vic_link(vic_t *vic1, vic_t *vic2, const char *name)
{
    if (vic1->abstraction & EF_PROCESS || vic2->abstraction & EF_PROCESS)
    {
        _vic_link_helper(vic1, vic2, name, ZMQ_DEALER, "ipc:///tmp/");
    }
    else
    {
        _vic_link_helper(vic1, vic2, name, ZMQ_PAIR, "inproc://");
    }
}

void vic_ef_start(vic_ef_t *ef)
{
    if (ef->routine)
        ef->vic->start(ef);
}

void vic_ef_wait(vic_ef_t *ef)
{
    if (ef->routine)
        ef->vic->wait(ef);
}

int vic_ef_send(vic_ef_t *ef, const char *name, const char *data)
{
    cc_for_each(&ef->vic->links, link)
    {
        char addr[ADDR_BUFFER_LEN] = {};
        strcpy(addr, link->zmq_transport_prefix);
        strcat(addr, name);

        if (strcmp(link->zmq_addr, addr) == 0)
        {
            zstr_send(link->zmq_sock, data);
            return 1;
        }
    }

    return 0;
}

char *vic_ef_recv(vic_ef_t *ef, const char *name)
{
    cc_for_each(&ef->vic->links, link)
    {
        char addr[ADDR_BUFFER_LEN] = {};
        strcpy(addr, link->zmq_transport_prefix);
        strcat(addr, name);

        if (strcmp(link->zmq_addr, addr) == 0)
            return zstr_recv(link->zmq_sock);
    }

    return NULL;
}