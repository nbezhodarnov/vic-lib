#include "vic.h"

#define _GNU_SOURCE

#include "features.h"
#include "pthread.h"
#include "stdlib.h"

#include <string.h>
#include <czmq.h>
#include <sys/syscall.h>

#define CC_NO_SHORT_NAMES
#include "third_party/cc/cc.h"

#define ADDR_BUFFER_LEN 256

#define WAIT_TIMEOUT 3

// Structure representing a link between two virtual isolation contexts
typedef struct
{
    int zmq_type;               // The type of the zmq socket
    char *zmq_transport_prefix; // The transport prefix of the zmq socket
    char *zmq_addr;             // The full address of the zmq socket (prefix + name)

    zsock_t *zmq_sock; // The zmq socket
    int zmq_bind;      // 1 if the socket is bound, 0 if the socket is connected
} _vic_link_t;

typedef cc_list(_vic_link_t) _vic_link_list_t;

enum _wait_result_t
{
    DONE = 0,
    NOT_DONE = 1
};

struct _vic_t
{
    enum vic_abstraction_t abstraction; // The abstraction of the virtual isolation context
    void *data;                         // Pointer to the data that the virtual isolation context will use
    vic_ef_t *ef;                       // Pointer to the root execution flow of the virtual isolation context (must be one-to-one only)

    _vic_link_list_t links; // Pointer to the linked list of links between virtual isolation contexts

    void (*start)(vic_t *);               // Pointer to the function that will start the execution flow
    enum _wait_result_t (*wait)(vic_t *); // Pointer to the function that will wait for the execution flow to finish

    void (*destroy)(vic_t *); // Pointer to the function that will destroy the execution flow cleaning up all the resources
} _vic_t;

// Structure representing an execution flow
struct _vic_ef_t
{
    vic_t *vic;           // Pointer to the virtual isolation context that the execution flow belongs to
    pthread_mutex_t lock; // Mutex lock for vic transformation

    void (*routine)(vic_t *);  // Pointer to the routine that the execution flow will execute
    void (*finished)(vic_t *); // Pointer to the function that will be called when the execution flow is about to be destroyed
} _vic_ef_t;

typedef cc_list(vic_t *) _vic_list_t;

_vic_list_t vic_list;

typedef cc_list(unsigned int) _uint_list_t;

_uint_list_t thread_tid_list;

pthread_t *vic_transform_preparation_thread = NULL;
int terminate_preparation_thread = 0;

void _vic_transform_thread_to_process(vic_t *vic, pid_t pid);
void _vic_transform_process_to_thread(vic_t *vic);

void _vic_start_helper(vic_t *vic);

int _get_threads_number()
{
    int result = 0;
    DIR *dir = opendir("/proc/self/task");
    if (dir)
    {
        while (readdir(dir))
        {
            result++;
        }
        closedir(dir);
    }
    return result;
}

void _wait_for_external_signal(zsock_t *socket, const char *signal)
{
    char *received_signal = NULL;
    while (received_signal == NULL || strcmp(received_signal, signal) != 0)
    {
        received_signal = zstr_recv(socket);
    }
    free(received_signal);
}

void _wait_for_external_signal_or_terminate(zsock_t *socket, const char *signal)
{
    zsock_set_rcvtimeo(socket, WAIT_TIMEOUT * 1000);

    char *received_signal = NULL;
    while (received_signal == NULL || strcmp(received_signal, signal) != 0)
    {
        if (terminate_preparation_thread)
        {
            zsock_destroy(&socket);
            pthread_exit(NULL);
        }

        received_signal = zstr_recv(socket);
    }

    free(received_signal);
}

void *vic_transform_prepare()
{
    for (;;)
    {
        pid_t main_pid = getpid();

        char *address = malloc(ADDR_BUFFER_LEN);
        snprintf(address, ADDR_BUFFER_LEN, "ipc:///tmp/vic_transform_prepare_%d", getpid());

        zsock_t *socket = zsock_new(ZMQ_DEALER);
        zsock_bind(socket, address);

        _wait_for_external_signal_or_terminate(socket, "prepare");

        cc_for_each(&vic_list, vic_ptr)
        {
            vic_t *vic = *vic_ptr;
            if (vic->abstraction & EF_THREAD)
            {
                pthread_mutex_lock(&vic->ef->lock);
                cc_for_each(&vic->links, link)
                {
                    zsock_destroy(&link->zmq_sock);
                    link->zmq_sock = NULL;
                }
            }
            else if (vic->abstraction & EF_PROCESS)
            {
                // TODO: Send signals to other processes to prepare for transformation
            }
        }

        zsock_destroy(&socket);
        socket = NULL;

        zsys_shutdown();

        int current_threads_number = _get_threads_number();

        socket = zsock_new(ZMQ_DEALER);
        zsock_bind(socket, address);

        const char *ready_signal = "ready";
        zstr_send(socket, ready_signal);
        zstr_sendf(socket, "%u", cc_size(&thread_tid_list));
        cc_for_each(&thread_tid_list, thread_id)
        {
            zstr_sendf(socket, "%u", *thread_id);
        }

        zsock_destroy(&socket);
        socket = NULL;

        free(address);

        zsys_shutdown();

        while (getpid() == main_pid && _get_threads_number() == current_threads_number)
        {
            sleep(1);
        }

        *vic_transform_preparation_thread = pthread_self();

        address = malloc(ADDR_BUFFER_LEN);
        snprintf(address, ADDR_BUFFER_LEN, "ipc:///tmp/vic_transform_prepare_%d", getpid());

        socket = zsock_new(ZMQ_DEALER);
        zsock_bind(socket, address);

        _wait_for_external_signal(socket, "start");

        zsock_destroy(&socket);
        socket = NULL;

        free(address);

        unsigned int *thread_tid = cc_first(&thread_tid_list);
        cc_for_each(&vic_list, vic_ptr)
        {
            vic_t *vic = *vic_ptr;

            if (vic->abstraction & EF_THREAD)
            {
                _vic_transform_thread_to_process(vic, (pid_t)*thread_tid);
            }
            else if (vic->abstraction & EF_PROCESS)
            {
                _vic_transform_process_to_thread(vic);
            }

            thread_tid = cc_next(&thread_tid_list, thread_tid);
        }

        thread_tid = cc_first(&thread_tid_list);
        cc_for_each(&vic_list, vic_ptr)
        {
            vic_t *vic = *vic_ptr;

            if (vic->abstraction & EF_PROCESS && (unsigned int)getpid() != *thread_tid) {
                thread_tid = cc_next(&thread_tid_list, thread_tid);
                pthread_mutex_unlock(&vic->ef->lock);
                continue;
            }

            _vic_start_helper(vic);

            pthread_mutex_unlock(&vic->ef->lock);

            thread_tid = cc_next(&thread_tid_list, thread_tid);
        }

        cc_cleanup(&thread_tid_list);
    }
}

void _send_exit_signal_to_prepare_thread()
{
    terminate_preparation_thread = 1;
}

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

    cc_init(&vic_list);
    cc_init(&thread_tid_list);

    vic_transform_preparation_thread = malloc(sizeof(pthread_t));
    pthread_create(vic_transform_preparation_thread, NULL, vic_transform_prepare, NULL);

    return main_vic;
}

void _vic_destroy_helper(vic_t *vic)
{
    cc_for_each(&vic->links, link)
    {
        zstr_free(&link->zmq_addr);
        zstr_free(&link->zmq_transport_prefix);

        if (link->zmq_sock)
            zsock_destroy(&link->zmq_sock);
    }
}

void vic_destroy(vic_t *vic)
{
    assert(vic->ef == NULL);

    _vic_destroy_helper(vic);

    cc_cleanup(&vic->links);

    if (vic->destroy != NULL)
        vic->destroy(vic);

    free(vic);

    if (cc_size(&vic_list) == 0)
    {
        _send_exit_signal_to_prepare_thread();
        pthread_join(*vic_transform_preparation_thread, NULL);
        free(vic_transform_preparation_thread);
        vic_transform_preparation_thread = NULL;
        cc_cleanup(&thread_tid_list);
    }
    else
    {
        vic_t **vic_element = cc_first(&vic_list);
        for (; vic_element != cc_end(&vic_list); vic_element = cc_next(&vic_list, vic_element))
        {
            if (*vic_element == vic)
            {
                vic_element = cc_erase(&vic_list, vic_element);
                break;
            }
        }
    }
}

void vic_ef_destroy(vic_ef_t *ef)
{
    if (ef->finished != NULL)
    {
        vic_t *vic = ef->vic;
        ef->finished(vic);
    }

    ef->vic->ef = NULL;
    free(ef);
}

void *_vic_thread_start_helper(void *data)
{
    pid_t main_pid = getpid();

    vic_t *vic = (vic_t *)data;
    cc_push(&thread_tid_list, syscall(__NR_gettid));
    vic->ef->routine(vic);

    if (getpid() != main_pid)
    {
        vic_ef_destroy(vic->ef);
        vic_destroy(vic);

        _send_exit_signal_to_prepare_thread();
        pthread_join(*vic_transform_preparation_thread, NULL);
        free(vic_transform_preparation_thread);
        vic_transform_preparation_thread = NULL;
        cc_cleanup(&thread_tid_list);

        zsys_shutdown();

        exit(EXIT_SUCCESS);
    }

    return NULL;
}

void _vic_start_helper(vic_t *vic)
{
    cc_for_each(&vic->links, link)
    {
        link->zmq_sock = zsock_new(link->zmq_type);
        zsock_set_sndtimeo(link->zmq_sock, WAIT_TIMEOUT * 1000);
        zsock_set_rcvtimeo(link->zmq_sock, WAIT_TIMEOUT * 1000);

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
void _vic_start_thread(vic_t *vic)
{
    _vic_start_helper(vic);

    vic->data = malloc(sizeof(pthread_t));
    pthread_create((pthread_t *)vic->data, NULL, _vic_thread_start_helper, vic);
}

// Waiting function for an execution flow that is a thread
enum _wait_result_t _vic_wait_thread(vic_t *vic)
{
    struct timespec ts;

    pthread_t *thread = (pthread_t *)vic->data;

    ts.tv_sec = WAIT_TIMEOUT;
    ts.tv_nsec = 0;

    int result = pthread_timedjoin_np(*thread, NULL, &ts);
    if (result == ETIMEDOUT)
    {
        return NOT_DONE;
    }
    return DONE;
}

void _vic_destroy_thread(vic_t *vic)
{
    pthread_t *thread = (pthread_t *)vic->data;
    free(thread);
}

void _vic_destroy_process(vic_t *vic)
{
}

vic_ef_t *vic_ef_get(vic_t *vic)
{
    return vic->ef;
}

// Starting function for an execution flow that is a process
void _vic_start_process(vic_t *vic)
{
    // TODO : Check for the errors of fork call

    vic->data = malloc(sizeof(pid_t));
    *((pid_t *)vic->data) = fork();

    // If the data is 0, then we are in the child process
    if (vic->data == 0)
    {
        zsys_shutdown();

        _vic_start_helper(vic);

        vic->ef->routine(vic);

        vic_ef_destroy(vic->ef);
        vic_destroy(vic);

        exit(EXIT_SUCCESS);
    }
}

enum _wait_result_t waitpid_with_timeout(pid_t pid, int options, int timeout_seconds)
{
    fd_set set;
    struct timeval timeout;
    int status;

    // Initialize the file descriptor set and timeout
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set); // Add stdin as a dummy file descriptor
    timeout.tv_sec = timeout_seconds;
    timeout.tv_usec = 0;

    // Wait for the child process or timeout
    int ready = select(1, &set, NULL, NULL, &timeout);

    if (ready == -1)
    {
        perror("select");
        exit(EXIT_FAILURE);
    }
    else if (ready == 0)
    {
        if (waitpid(pid, &status, options | WNOHANG) == pid)
        {
            // Child process became zombie
            return DONE;
        }

        // Timeout occurred
        return NOT_DONE;
    }
    else
    {
        // Child process terminated
        waitpid(pid, &status, options);
        return DONE;
    }
}

// Waiting function for an execution flow that is a process
enum _wait_result_t _vic_wait_process(vic_t *vic)
{
    // TODO: Check if the process is launched
    pid_t pid = *(pid_t *)vic->data;
    return waitpid_with_timeout(pid, 0, WAIT_TIMEOUT);
}

typedef struct {
    int zmq_type;
    char* transport_prefix;
} transport_params_t;

transport_params_t _transport_params[] = {
    {ZMQ_DEALER, "ipc:///tmp/"},
    {ZMQ_PAIR, "inproc://"}
};

transport_params_t* _get_transport_params(vic_t* vic1, vic_t* vic2) {
    if (vic1->abstraction & EF_PROCESS || vic2->abstraction & EF_PROCESS)
    {
        return &_transport_params[0];
    }
    else
    {
        return &_transport_params[1];
    }
}

void _vic_transform_thread_to_process(vic_t *vic, pid_t pid)
{
    vic->abstraction = EF_PROCESS;
    vic->start = _vic_start_process;
    vic->wait = _vic_wait_process;
    vic->destroy = _vic_destroy_process;

    free(vic->data);
    vic->data = malloc(sizeof(pid_t));
    *((pid_t *)vic->data) = pid;

    _vic_link_list_t links;
    cc_init_clone(&links, &vic->links);

    cc_cleanup(&vic->links);

    cc_for_each(&links, link)
    {
        char* name = strdup(link->zmq_addr + strlen(link->zmq_transport_prefix));

        transport_params_t* transport_params = _get_transport_params(vic, vic);

        link->zmq_type = transport_params->zmq_type;

        zstr_free(&link->zmq_transport_prefix);
        link->zmq_transport_prefix = strdup(transport_params->transport_prefix);

        zstr_free(&link->zmq_addr);
        
        int total_len = strlen(link->zmq_transport_prefix) + strlen(name) + 1;
        char *addr = (char *)calloc(total_len, sizeof(char));

        strcpy(addr, link->zmq_transport_prefix);
        strcat(addr, name);

        link->zmq_addr = addr;

        cc_push(&vic->links, *link);
        free(name);
    }

    cc_cleanup(&links);
}

void _vic_transform_process_to_thread(vic_t *vic)
{
    vic->abstraction = EF_THREAD;
    vic->start = _vic_start_thread;
    vic->wait = _vic_wait_thread;
    vic->destroy = _vic_destroy_thread;

    free(vic->data);
    vic->data = malloc(sizeof(pthread_t));
    *((pthread_t *)vic->data) = pthread_self();
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

    cc_push(&vic_list, vic);

    return vic;
}

vic_ef_t *vic_ef_create(vic_t *vic, void (*start_routine)(vic_t *), void (*finished)(vic_t *))
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
    transport_params_t* transport_params = _get_transport_params(vic1, vic2);
    _vic_link_helper(vic1, vic2, name, transport_params->zmq_type, transport_params->transport_prefix);
}

void vic_ef_start(vic_ef_t *ef)
{
    if (ef->routine)
    {
        vic_t *vic = ef->vic;
        vic->start(vic);
    }
}

void vic_ef_wait(vic_ef_t *ef)
{
    if (ef->routine)
    {
        vic_t *vic = ef->vic;
        enum _wait_result_t wait_result = NOT_DONE;

        while (wait_result == NOT_DONE)
        {
            pthread_mutex_lock(&ef->lock);
            wait_result = vic->wait(vic);
            pthread_mutex_unlock(&ef->lock);
        }
    }
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
            int result = -1;
            while (result != 0)
            {
                pthread_mutex_lock(&ef->lock);
                result = zstr_send(link->zmq_sock, data);
                pthread_mutex_unlock(&ef->lock);
            }
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
        {
            char *result = NULL;
            while (result == NULL)
            {
                pthread_mutex_lock(&ef->lock);
                result = zstr_recv(link->zmq_sock);
                pthread_mutex_unlock(&ef->lock);
            }
            return result;
        }
    }

    return NULL;
}