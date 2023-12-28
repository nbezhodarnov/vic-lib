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

    ef_send(ef, "test3", "hello");

    data = ef_recv(ef, "test3");

    printf("routine3 from test3: %s\n", data);

    free(data);

    printf("routine3 finished\n");
};

void routine4(ef_t *ef) {
    printf("routine4 started\n");

    printf("pid: %d\n", getpid());

    ef_send(ef, "test3", "hello");

    char * data = ef_recv(ef, "test3");

    printf("routine4 from test3: %s\n", data);

    free(data);

    printf("routine4 finished\n");
}

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
    ef_t *ef4 = ef_create(routine4, EF_PROCESS, NULL);

    // ef1 and ef2 now are linked which allows them to communicate with each other
    ef_link(ef1, ef2, "test1");
    ef_link(ef1, ef3, "test2");
    ef_link(ef3, ef4, "test3");


    printf("main pid: %d\n", getpid());

    ef_start(ef1);
    ef_start(ef2);
    ef_start(ef3);
    ef_start(ef4);

    ef_wait(ef1);
    ef_wait(ef2);
    ef_wait(ef3);
    ef_wait(ef4);

    ef_destroy(ef1);
    ef_destroy(ef2);
    ef_destroy(ef3);
    ef_destroy(ef4);

    ef_cleanup(main_ef);

    return 0;
}