#include "ef_lib.h"
#include "pthread.h"
#include "stdlib.h"

#include <string.h>
#include <czmq.h>

#define CC_NO_SHORT_NAMES
#include "cc.h"

#define ADDR_BUFFER_LEN 256

struct _vic_t
{
    enum vic_abstraction_t abstraction; // The abstraction of the virtual isolation context
    void *data;                         // Pointer to the data that the virtual isolation context will use
    vic_ef_t *ef;                       // Pointer to the root execution flow of the virtual isolation context

    void (*start)(vic_ef_t *); // Pointer to the function that will start the execution flow
    void (*wait)(vic_ef_t *);  // Pointer to the function that will wait for the execution flow to finish

    void (*destroy)(vic_t *); // Pointer to the function that will destroy the execution flow cleaning up all the resources
};

typedef struct
{
    int zmq_type;
    char *zmq_transport_prefix;
    char *zmq_addr;

    zsock_t *zmq_sock;
    int zmq_bind;
} _vic_ef_link_t;

typedef cc_list(_vic_ef_link_t) _vic_ef_link_list_t;

// Structure representing an execution flow
struct _vic_ef_t
{
    vic_t *vic; // Pointer to the virtual isolation context that the execution flow belongs to

    void (*routine)(vic_ef_t *);  // Pointer to the routine that the execution flow will execute
    void (*finished)(vic_ef_t *); // Pointer to the function that will be called when the execution flow is about to be destroyed

    _vic_ef_link_list_t links; // Pointer to the linked list of links between this execution flows
};

vic_t *_vic_new()
{
    vic_t *vic = malloc(sizeof(vic_t));
    vic->abstraction = 0;
    vic->data = NULL;
    vic->ef = NULL;

    vic->start = NULL;
    vic->wait = NULL;
    vic->destroy = NULL;

    return vic;
}

vic_ef_t *_ef_new()
{
    vic_ef_t *ef = malloc(sizeof(vic_ef_t));
    cc_init(&ef->links);

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
    if (vic->destroy != NULL)
        vic->destroy(vic);

    assert(vic->ef == NULL);
    free(vic);
}

void vic_ef_destroy(vic_ef_t *ef)
{
    if (ef->finished != NULL)
        ef->finished(ef);

    cc_for_each(&ef->links, link)
    {
        zstr_free(&link->zmq_addr);
        zstr_free(&link->zmq_transport_prefix);

        if (link->zmq_sock)
            zsock_destroy(&link->zmq_sock);
    }

    cc_cleanup(&ef->links);

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
    cc_for_each(&ef->links, link)
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

        vic_ef_destroy(ef);

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

void _vic_ef_link_init_helper(_vic_ef_link_t *link)
{
    link->zmq_type = 0;
    link->zmq_transport_prefix = NULL;
    link->zmq_addr = NULL;
    link->zmq_sock = NULL;
    link->zmq_bind = 0;
}

void _vic_ef_link_helper(vic_ef_t *ef1, vic_ef_t *ef2, const char *name, int zmq_type, const char *transport_prefix)
{
    _vic_ef_link_t ef1_link;
    _vic_ef_link_init_helper(&ef1_link);
    _vic_ef_link_t ef2_link;
    _vic_ef_link_init_helper(&ef2_link);

    ef1_link.zmq_bind = 0;
    ef2_link.zmq_bind = 1;

    ef1_link.zmq_type = zmq_type;
    ef1_link.zmq_transport_prefix = strdup(transport_prefix);

    ef2_link.zmq_type = zmq_type;
    ef2_link.zmq_transport_prefix = strdup(transport_prefix);

    int total_len = strlen(transport_prefix) + strlen(name) + 1;
    char *addr = (char *)calloc(total_len, sizeof(char));

    strcpy(addr, transport_prefix);
    strcat(addr, name);

    ef1_link.zmq_addr = addr;
    ef2_link.zmq_addr = strdup(addr);

    cc_push(&ef1->links, ef1_link);
    cc_push(&ef2->links, ef2_link);
}

void vic_ef_link(vic_ef_t *ef1, vic_ef_t *ef2, const char *name)
{
    if (ef1->vic->abstraction & EF_PROCESS || ef2->vic->abstraction & EF_PROCESS)
    {
        _vic_ef_link_helper(ef1, ef2, name, ZMQ_DEALER, "ipc:///tmp/");
    }
    else
    {
        _vic_ef_link_helper(ef1, ef2, name, ZMQ_PAIR, "inproc://");
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
    cc_for_each(&ef->links, link)
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
    cc_for_each(&ef->links, link)
    {
        char addr[ADDR_BUFFER_LEN] = {};
        strcpy(addr, link->zmq_transport_prefix);
        strcat(addr, name);

        if (strcmp(link->zmq_addr, addr) == 0)
            return zstr_recv(link->zmq_sock);
    }

    return NULL;
}