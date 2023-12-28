#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "ef_lib.h"

void routine3(ef_t *ef){
    printf("routine3 started\n");

    printf("pid: %d\n", getpid());

    ef_send(ef, "test2", "hello");

    char * data = ef_recv(ef, "test2");

    printf("routine3 from test2: %s\n", data);

    free(data);

    printf("routine3 finished\n");
};

// You can create and link other execution flows from within an execution flow
// So basically, it's like a nesting, because this routine can be called from a thread or a process

// P.S. I'm not sure if this is the best way to acheive nesting,
// I think it won't work if this routine will be executed in a process and we will want to create another execution flow
// from within this routine and we will want to link it to any of execution flows of the parent process
// So, I think we should determine the hole configuration of the execution flows before starting them
// in the main function
void routine1(ef_t *ef)
{
    printf("routine1 started\n");

    char * data = ef_recv(ef, "test1");

    printf("routine1 from test1: %s\n", data);

    free(data);

    ef_send(ef, "test1", "bye");

    data = ef_recv(ef, "test2");

    printf("routine1 from test2: %s\n", data);

    free(data);

    ef_send(ef, "test2", "bye");

    printf("routine1 finished\n");
};

void routine2(ef_t *ef)
{
    printf("routine2 started\n");

    ef_send(ef, "test1", "hello");

    char * data = ef_recv(ef, "test1");

    printf("routine2 from test1: %s\n", data);

    free(data);

    printf("routine2 finished\n");
};

int main(int argc, char **argv)
{
    ef_t *main_ef = ef_init();

    ef_t *ef1 = ef_create(routine1, EF_THREAD, NULL);
    ef_t *ef2 = ef_create(routine2, EF_THREAD, NULL);
    ef_t *ef3 = ef_create(routine3, EF_PROCESS, NULL);

    // ef1 and ef2 now are linked which allows them to communicate with each other
    ef_link(ef1, ef2, "test1");
    ef_link(ef1, ef3, "test2");


    printf("main pid: %d\n", getpid());

    ef_start(ef1);
    ef_start(ef2);
    ef_start(ef3);

    ef_wait(ef1);
    ef_wait(ef2);
    ef_wait(ef3);

    ef_destroy(ef1);
    ef_destroy(ef2);
    ef_destroy(ef3);

    ef_cleanup(main_ef);

    return 0;
}