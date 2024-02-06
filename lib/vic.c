#include "vic.h"

#include "pause_thread.h"

#define _GNU_SOURCE

#include "features.h"
#include "pthread.h"
#include "stdlib.h"

#include <string.h>
#include <czmq.h>
#include <sys/syscall.h>

#include <sys/resource.h>

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

struct _vic_with_thread_info_t
{
    vic_t *vic;
    unsigned int tid;
    pthread_t thread;
    bool executing;
} _vic_with_thread_info_t;

typedef cc_list(struct _vic_with_thread_info_t) _vic_list_t;

_vic_list_t vic_list;

pthread_t vic_transform_preparation_thread = 0;
int terminate_preparation_thread = 0;

pid_t main_pid = 0;

enum vic_abstraction_t current_abstraction;

void _vic_transform_thread_to_process(vic_t *vic, pid_t pid);
void _vic_transform_process_to_thread(vic_t *vic, pthread_t thread);

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

int _get_children_processes_number(pid_t parent_pid) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/task/%d/children", parent_pid, parent_pid);

    FILE *children_file = fopen(path, "r");
    if (children_file == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    int processes_number = 0;
    char line[256];
    while (fgets(line, sizeof(line), children_file) != NULL) {
        // The contents of the children file are a list of space-separated child thread IDs
        char *token = strtok(line, " ");
        while (token != NULL) {
            pid_t childPid = atoi(token);
            processes_number++;

            // Move to the next token
            token = strtok(NULL, " ");
        }
    }

    fclose(children_file);

    return processes_number;
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

void _vic_disconnect_links(vic_t *vic)
{
    cc_for_each(&vic->links, link)
    {
        zsock_destroy(&link->zmq_sock);
        link->zmq_sock = NULL;
    }
}

void perform_transform_threads_to_processes()
{
    pid_t main_pid = getpid();

    char address[ADDR_BUFFER_LEN] = {};
    snprintf(address, ADDR_BUFFER_LEN, "ipc:///tmp/vic_transform_prepare_%d", getpid());

    cc_for_each(&vic_list, vic_ptr)
    {
        vic_t *vic = vic_ptr->vic;
        if (vic->abstraction & EF_THREAD && vic_ptr->executing)
        {
            pthread_mutex_lock(&vic->ef->lock);
            _vic_disconnect_links(vic);
        }
        else if (vic->abstraction & EF_PROCESS)
        {
            // TODO: Send signals to other processes to prepare for transformation
        }
    }

    zsys_shutdown();

    int current_threads_number = _get_threads_number();

    zsock_t* socket = zsock_new(ZMQ_DEALER);
    zsock_bind(socket, address);

    const char *ready_signal = "ready";
    zstr_send(socket, ready_signal);

    cc_list(unsigned int) tid_list;
    cc_init(&tid_list);
    cc_for_each(&vic_list, vic_ptr)
    {
        if (vic_ptr->executing)
        {
            cc_push(&tid_list, vic_ptr->tid);
        }
    }

    zstr_sendf(socket, "%u", cc_size(&tid_list));
    cc_for_each(&tid_list, tid)
    {
        zstr_sendf(socket, "%u", *tid);
    }

    zsock_destroy(&socket);
    socket = NULL;

    zsys_shutdown();

    while (getpid() == main_pid && _get_threads_number() == current_threads_number)
    {
        sleep(1);
    }

    vic_transform_preparation_thread = pthread_self();

    snprintf(address, ADDR_BUFFER_LEN, "ipc:///tmp/vic_transform_prepare_%d", getpid());

    socket = zsock_new(ZMQ_DEALER);
    zsock_bind(socket, address);

    _wait_for_external_signal(socket, "start");

    zsock_destroy(&socket);
    socket = NULL;

    cc_for_each(&vic_list, vic_ptr)
    {
        vic_t *vic = vic_ptr->vic;
        unsigned int thread_tid = vic_ptr->tid;

        if (vic->abstraction & EF_THREAD)
        {
            _vic_transform_thread_to_process(vic, (pid_t)thread_tid);
        }
    }

    cc_for_each(&vic_list, vic_ptr)
    {
        vic_t *vic = vic_ptr->vic;
        unsigned int thread_tid = vic_ptr->tid;

        if (!vic_ptr->executing)
        {
            continue;
        }

        if (vic->abstraction & EF_PROCESS && (unsigned int)getpid() != thread_tid)
        {
            pthread_mutex_unlock(&vic->ef->lock);
            continue;
        }

        _vic_start_helper(vic);

        pthread_mutex_unlock(&vic->ef->lock);
    }
}

void* _infinite_loop(void* data)
{
    struct _vic_with_thread_info_t* vic_ptr = (struct _vic_with_thread_info_t*)data;
    vic_ptr->tid = syscall(__NR_gettid);
    vic_ptr->executing = true;
    vic_ptr->thread = pthread_self();

    for (;;)
    {
        sleep(1);
    }
}

enum _wait_result_t _vic_wait_process(vic_t *vic);

void perform_transform_processes_to_threads()
{
    pid_t process_pid = getpid();

    char address[ADDR_BUFFER_LEN] = {};
    snprintf(address, ADDR_BUFFER_LEN, "ipc:///tmp/vic_transform_prepare_%d", process_pid);

    printf("Process PID: %d\n", process_pid);
    printf("Main PID: %d\n", main_pid);
    if (process_pid == main_pid)
    {
        zsock_t *socket = zsock_new(ZMQ_DEALER);
        zsock_bind(socket, address);

        printf("Waiting for stack size\n");

        char *stack_size_str = zstr_recv(socket);
        size_t stack_size = (unsigned long)atol(stack_size_str);
        zstr_free(&stack_size_str);

        printf("Stack size received: %u\n", stack_size);

        pthread_attr_t attr;
        pthread_attr_init(&attr);
        
        size_t current_stack_size = 0;
        pthread_attr_getstacksize(&attr, &current_stack_size);

        if (current_stack_size < stack_size)
        {
            pthread_attr_setstacksize(&attr, stack_size);
        }

        struct process_transformation_info_t
        {
            pid_t pid;
            pthread_t thread;
        };

        printf("Locking all locks\n");

        cc_list(struct process_transformation_info_t) threads_list;
        cc_init(&threads_list);

        cc_for_each(&vic_list, vic_ptr)
        {
            vic_t *vic = vic_ptr->vic;
            if (vic->abstraction & EF_PROCESS && vic->ef != NULL)
            {
                if (pthread_mutex_trylock(&vic->ef->lock) == 0)
                {
                    continue;
                }

                pthread_mutex_unlock(&vic->ef->lock);
                pthread_mutex_lock(&vic->ef->lock);
            }
        }

        printf("All locks are locked\n");

        int processes_number = _get_children_processes_number(process_pid);

        printf("Creating threads\n");

        cc_for_each(&vic_list, vic_ptr)
        {
            vic_t *vic = vic_ptr->vic;
            if (vic->abstraction & EF_PROCESS && _vic_wait_process(vic) == NOT_DONE)
            {
                pthread_t thread;
                pthread_create(&thread, &attr, _infinite_loop, (void*)vic_ptr);

                while(vic_ptr->thread != thread);

                struct process_transformation_info_t thread_info = {*(pid_t*)(vic_ptr->vic->data), vic_ptr->tid};
                cc_push(&threads_list, thread_info);

                pthread_pause(thread);

                pthread_resume(thread);
            }
        }

        printf("Threads created\n");

        assert(cc_size(&threads_list) == processes_number);

        printf("Sending ready signal\n");

        pthread_attr_setstacksize(&attr, current_stack_size);
        pthread_attr_destroy(&attr);

        zstr_send(socket, "ready");

        printf("Ready signal sent\n");

        printf("Sending threads info\n");

        zstr_sendf(socket, "%u", cc_size(&threads_list));
        cc_for_each(&threads_list, thread_info)
        {
            zstr_sendf(socket, "%u %u", thread_info->pid, thread_info->thread);
        }

        printf("Threads info sent\n");

        zsock_destroy(&socket);
        socket = NULL;

        zsys_shutdown();

        cc_cleanup(&threads_list);

        printf("Waiting for transformation\n");

        while (_get_children_processes_number(process_pid) == processes_number)
        {
            sleep(1);
        }

        printf("Transformation finished\n");

        socket = zsock_new(ZMQ_DEALER);
        zsock_bind(socket, address);

        printf("Waiting for start signal\n");

        _wait_for_external_signal(socket, "start");

        printf("Start signal received\n");

        zsock_destroy(&socket);
        socket = NULL;

        printf("Converting\n");

        cc_for_each(&vic_list, vic_ptr)
        {
            vic_t *vic = vic_ptr->vic;
            if (!(vic->abstraction & EF_PROCESS))
            {
                continue;
            }

            printf("Converting process to thread\n");

            _vic_transform_process_to_thread(vic, vic_ptr->thread);
        }

        printf("Converted\n");

        printf("Resuming threads\n");

        cc_for_each(&vic_list, vic_ptr)
        {
            vic_t *vic = vic_ptr->vic;
            if (vic->abstraction & EF_THREAD && vic_ptr->executing)
            {
                _vic_start_helper(vic);

                pthread_mutex_unlock(&vic->ef->lock);
            }
        }

        printf("Resumed threads\n");

        return;
    }

    printf("Parent process: %d\n", main_pid);
    printf("Current process: %d\n", process_pid);
    pthread_t process_thread = 0;
    cc_for_each(&vic_list, vic_ptr)
    {
        vic_t *vic = vic_ptr->vic;
        printf("vic abstraction: %d\n", vic->abstraction);
        printf("vic_ptr tid: %u\n", vic_ptr->tid);
        printf("vic_ptr executing: %d\n", vic_ptr->executing);
        if (vic->abstraction & EF_PROCESS && vic_ptr->tid == (unsigned int)process_pid && vic_ptr->executing)
        {
            pthread_mutex_lock(&vic->ef->lock);
            _vic_disconnect_links(vic);
            process_thread = vic_ptr->thread;
            break;
        }
    }

    if (process_thread == 0)
    {
        zsock_t *socket = zsock_new(ZMQ_DEALER);
        zsock_bind(socket, address);

        zstr_send(socket, "done");
        zsock_destroy(&socket);
        socket = NULL;
        pthread_exit(NULL);
    }

    printf("Pausing process thread\n");
    pthread_pause(process_thread);
    printf("Process thread paused\n");

    pthread_resume(process_thread);

    zsock_t *socket = zsock_new(ZMQ_DEALER);
    zsock_bind(socket, address);

    zstr_send(socket, "ready");
    zsock_destroy(&socket);
    socket = NULL;

    zsys_shutdown();

    pthread_exit(NULL);
}

void *vic_transform_prepare()
{
    for (;;)
    {
        char address[ADDR_BUFFER_LEN] = {};
        snprintf(address, ADDR_BUFFER_LEN, "ipc:///tmp/vic_transform_prepare_%d", getpid());

        zsock_t *socket = zsock_new(ZMQ_DEALER);
        zsock_bind(socket, address);

        _wait_for_external_signal_or_terminate(socket, "prepare");

        zsock_destroy(&socket);
        socket = NULL;

        if (current_abstraction == EF_THREAD)
        {
            perform_transform_threads_to_processes();
        }
        else if (current_abstraction == EF_PROCESS)
        {
            perform_transform_processes_to_threads();
        }
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
    pthread_pause_enable();

    vic_t *main_vic = _vic_new();
    main_vic->abstraction = EF_THREAD;

    cc_init(&vic_list);

    pthread_create(&vic_transform_preparation_thread, NULL, vic_transform_prepare, NULL);

    main_pid = getpid();

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
        pthread_join(vic_transform_preparation_thread, NULL);
        vic_transform_preparation_thread = 0;
    }
    else
    {
        struct _vic_with_thread_info_t *vic_element = cc_first(&vic_list);
        for (; vic_element != cc_end(&vic_list); vic_element = cc_next(&vic_list, vic_element))
        {
            if (vic_element->vic == vic)
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

struct _vic_with_thread_info_t* _find_vic_with_thread_info(vic_t *vic)
{
    struct _vic_with_thread_info_t *result = NULL;
    cc_for_each(&vic_list, vic_ptr)
    {
        if (vic_ptr->vic == vic)
        {
            result = vic_ptr;
            break;
        }
    }
    return result;
}

void *_vic_thread_start_helper(void *data)
{
    pid_t main_pid = getpid();

    vic_t *vic = (vic_t *)data;
    struct _vic_with_thread_info_t *current_vic_ptr = _find_vic_with_thread_info(vic);
    current_vic_ptr->tid = syscall(__NR_gettid);
    current_vic_ptr->thread = pthread_self();
    current_vic_ptr->executing = true;

    vic->ef->routine(vic);

    current_vic_ptr->executing = false;

    if (getpid() != main_pid)
    {
        vic_ef_destroy(vic->ef);
        vic_destroy(vic);

        _send_exit_signal_to_prepare_thread();
        pthread_join(vic_transform_preparation_thread, NULL);
        vic_transform_preparation_thread = 0;

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
    if (*((pid_t *)vic->data) == 0)
    {
        zsys_shutdown();

        struct _vic_with_thread_info_t *current_vic_ptr = _find_vic_with_thread_info(vic);
        current_vic_ptr->tid = syscall(__NR_gettid);
        current_vic_ptr->thread = pthread_self();
        current_vic_ptr->executing = true;

        pthread_create(&vic_transform_preparation_thread, NULL, vic_transform_prepare, NULL);

        _vic_start_helper(vic);

        vic->ef->routine(vic);

        current_vic_ptr->executing = false;

        terminate_preparation_thread = true;
        pthread_join(vic_transform_preparation_thread, NULL);

        vic_ef_destroy(vic->ef);
        vic_destroy(vic);

        if (main_pid == getpid())
        {
            pthread_exit(NULL);
        }

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

void _vic_reinit_links(vic_t *vic)
{
    cc_for_each(&vic->links, link)
    {
        char* name = strdup(link->zmq_addr + strlen(link->zmq_transport_prefix));

        transport_params_t* transport_params = _get_transport_params(vic, vic);

        link->zmq_type = transport_params->zmq_type;

        free(link->zmq_transport_prefix);
        link->zmq_transport_prefix = strdup(transport_params->transport_prefix);

        free(link->zmq_addr);
        
        int total_len = strlen(link->zmq_transport_prefix) + strlen(name) + 1;
        char *addr = (char *)calloc(total_len, sizeof(char));

        strcpy(addr, link->zmq_transport_prefix);
        strcat(addr, name);

        link->zmq_addr = addr;

        cc_push(&vic->links, *link);
        free(name);
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

    _vic_reinit_links(vic);
}

void _vic_transform_process_to_thread(vic_t *vic, pthread_t thread)
{
    vic->abstraction = EF_THREAD;
    vic->start = _vic_start_thread;
    vic->wait = _vic_wait_thread;
    vic->destroy = _vic_destroy_thread;

    free(vic->data);
    vic->data = malloc(sizeof(pthread_t));
    *((pthread_t *)vic->data) = thread;

    _vic_reinit_links(vic);
}

vic_t *vic_create(enum vic_abstraction_t abstraction)
{
    vic_t *vic = _vic_new();

    if (abstraction & EF_THREAD)
    {
        current_abstraction = EF_THREAD;
        vic->abstraction = EF_THREAD;
        vic->start = _vic_start_thread;
        vic->wait = _vic_wait_thread;
        vic->destroy = _vic_destroy_thread;
    }
    else if (abstraction & EF_PROCESS)
    {
        current_abstraction = EF_PROCESS;
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

    struct _vic_with_thread_info_t vic_with_tid;
    vic_with_tid.vic = vic;
    vic_with_tid.tid = 0;
    vic_with_tid.thread = 0;
    vic_with_tid.executing = false;
    cc_push(&vic_list, vic_with_tid);

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