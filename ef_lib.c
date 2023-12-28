#include "ef_lib.h"
#include "pthread.h"
#include "stdlib.h"

#include <string.h>
#include <czmq.h>

#define CC_NO_SHORT_NAMES
#include "cc.h"

#define ADDR_BUFFER_LEN 256

typedef struct _ef_link
{
    int zmq_type;
    char *zmq_transport_prefix;
    char *zmq_addr;

    zsock_t *zmq_sock;
    int zmq_bind;
} _ef_link_t;

typedef cc_list(_ef_link_t) _ef_link_list_t;

// Structure representing an execution flow
struct _ef_t
{
    enum ef_abstraction_t abstraction; // The abstraction of the execution flow

    void (*routine)(ef_t *);  // Pointer to the routine that the execution flow will execute
    void (*finished)(ef_t *); // Pointer to the function that will be called when the execution flow is about to be destroyed

    _ef_link_list_t links; // Pointer to the linked list of links between this execution flows

    void *data;              // Pointer to the data that the execution flow will use
    void (*start)(ef_t *);   // Pointer to the function that will start the execution flow
    void (*wait)(ef_t *);    // Pointer to the function that will wait for the execution flow to finish
    void (*destroy)(ef_t *); // Pointer to the function that will destroy the execution flow cleaning up all the resources
};

ef_t *_ef_new()
{
    ef_t *ef = malloc(sizeof(ef_t));
    cc_init(&ef->links);

    ef->routine = NULL;
    ef->abstraction = 0;
    ef->finished = NULL;

    ef->data = NULL;
    ef->start = NULL;
    ef->wait = NULL;
    ef->destroy = NULL;

    return ef;
}

ef_t *ef_init()
{
    ef_t *main_ef = _ef_new();
    main_ef->abstraction = EF_PROCESS;

    return main_ef;
}

void ef_cleanup(ef_t *ef)
{
    ef_destroy(ef);
}

void *_ef_thread_start_helper(void *data)
{
    ef_t *ef = (ef_t *)data;
    ef->routine(ef);
    return NULL;
}

void _ef_start_helper(ef_t *ef)
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
void _ef_start_thread(ef_t *ef)
{
    _ef_start_helper(ef);

    ef->data = malloc(sizeof(pthread_t));
    pthread_create((pthread_t *)ef->data, NULL, _ef_thread_start_helper, ef);
}

// Waiting function for an execution flow that is a thread
void _ef_wait_thread(ef_t *ef)
{
    pthread_t *thread = (pthread_t *)ef->data;
    pthread_join(*thread, NULL);
}

void _ef_destroy_thread(ef_t *ef)
{
    pthread_t *thread = (pthread_t *)ef->data;
    free(thread);
}

void _ef_destroy_process(ef_t *ef)
{
}

// Starting function for an execution flow that is a process
void _ef_start_process(ef_t *ef)
{
    ef->data = (void *)(uintptr_t)fork();

    // If the data is 0, then we are in the child process
    if (ef->data == 0)
    {
        zsys_shutdown();

        _ef_start_helper(ef);

        ef->routine(ef);

        ef_destroy(ef);

        exit(EXIT_SUCCESS);
    }
}

// Waiting function for an execution flow that is a process
void _ef_wait_process(ef_t *ef)
{
    waitpid((pid_t)(uintptr_t)ef->data, NULL, 0);
}

ef_t *ef_create(void (*start_routine)(ef_t *), enum ef_abstraction_t abstraction, void (*finished)(ef_t *))
{
    ef_t *ef = _ef_new();
    ef->routine = start_routine;
    ef->finished = finished;

    if (abstraction & EF_THREAD)
    {
        ef->abstraction = EF_THREAD;
        ef->start = _ef_start_thread;
        ef->wait = _ef_wait_thread;
        ef->destroy = _ef_destroy_thread;
    }
    else if (abstraction & EF_PROCESS)
    {
        ef->abstraction = EF_PROCESS;
        ef->start = _ef_start_process;
        ef->wait = _ef_wait_process;
        ef->destroy = _ef_destroy_process;
    }
    else
    {
        printf("Invalid abstraction specified\n");
        exit(EXIT_FAILURE);
    }

    return ef;
}

void ef_destroy(ef_t *ef)
{
    if (ef->finished != NULL)
        ef->finished(ef);

    if (ef->destroy != NULL)
        ef->destroy(ef);

    cc_for_each(&ef->links, link)
    {
        zstr_free(&link->zmq_addr);
        zstr_free(&link->zmq_transport_prefix);
        zsock_destroy(&link->zmq_sock);
    }

    cc_cleanup(&ef->links);

    free(ef);
}

void _ef_link_helper(ef_t *ef1, ef_t *ef2, const char *name, int zmq_type, const char *transport_prefix)
{
    _ef_link_t ef1_link;
    _ef_link_t ef2_link;

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

void ef_link(ef_t *ef1, ef_t *ef2, const char *name)
{
    if (ef1->abstraction & EF_PROCESS || ef2->abstraction & EF_PROCESS)
    {
        _ef_link_helper(ef1, ef2, name, ZMQ_DEALER, "ipc:///tmp/");
    }
    else
    {
        _ef_link_helper(ef1, ef2, name, ZMQ_PAIR, "inproc://");
    }
}

void ef_start(ef_t *ef)
{
    ef->start(ef);
}

void ef_wait(ef_t *ef)
{
    ef->wait(ef);
}

int ef_send(ef_t *ef, const char *name, const char *data)
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

char *ef_recv(ef_t *ef, const char *name)
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