#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "lib/vic.h"

void routine3(vic_t *vic){
    vic_ef_t *ef = vic_ef_get(vic);

    printf("routine3 started\n");

    printf("pid: %d\n", getpid());

    vic_ef_send(ef, "test2", "hello");

    char * data = vic_ef_recv(ef, "test2");

    printf("routine3 from test2: %s\n", data);

    free(data);

    vic_ef_send(ef, "test3", "hello");

    data = vic_ef_recv(ef, "test3");

    printf("routine3 from test3: %s\n", data);

    free(data);

    printf("routine3 finished\n");
};

void routine4(vic_t *vic) {
    vic_ef_t *ef = vic_ef_get(vic);

    printf("routine4 started\n");

    printf("pid: %d\n", getpid());

    vic_ef_send(ef, "test3", "hello");

    char * data = vic_ef_recv(ef, "test3");

    printf("routine4 from test3: %s\n", data);

    free(data);

    printf("routine4 finished\n");
}

void routine1(vic_t *vic)
{
    vic_ef_t *ef = vic_ef_get(vic);

    printf("routine1 started\n");

    char * data = vic_ef_recv(ef, "test1");

    printf("routine1 from test1: %s\n", data);

    free(data);

    vic_ef_send(ef, "test1", "bye");

    data = vic_ef_recv(ef, "test2");

    printf("routine1 from test2: %s\n", data);

    free(data);

    vic_ef_send(ef, "test2", "bye");

    printf("routine1 finished\n");
};

void routine2(vic_t *vic)
{
    vic_ef_t *ef = vic_ef_get(vic);
    
    printf("routine2 started\n");

    vic_ef_send(ef, "test1", "hello");

    char * data = vic_ef_recv(ef, "test1");

    printf("routine2 from test1: %s\n", data);

    free(data);

    printf("routine2 finished\n");
};

int main(int argc, char **argv)
{
    vic_t *main_vic = vic_init();
    vic_t *vic1 = vic_create(EF_THREAD);
    vic_t *vic2 = vic_create(EF_THREAD);
    vic_t *vic3 = vic_create(EF_PROCESS);
    vic_t *vic4 = vic_create(EF_PROCESS);

    vic_ef_t *main_ef = vic_ef_create(main_vic, NULL, NULL);
    vic_ef_t *ef1 = vic_ef_create(vic1, routine1, NULL);
    vic_ef_t *ef2 = vic_ef_create(vic2, routine2, NULL);
    vic_ef_t *ef3 = vic_ef_create(vic3, routine3, NULL);
    vic_ef_t *ef4 = vic_ef_create(vic4, routine4, NULL);

    // ef1 and ef2 now are linked which allows them to communicate with each other
    vic_link(vic1, vic2, "test1");
    vic_link(vic1, vic3, "test2");
    vic_link(vic3, vic4, "test3");

    printf("main pid: %d\n", getpid());

    vic_ef_start(ef1);
    vic_ef_start(ef2);
    vic_ef_start(ef3);
    vic_ef_start(ef4);

    vic_ef_wait(ef1);
    vic_ef_wait(ef2);
    vic_ef_wait(ef3);
    vic_ef_wait(ef4);

    vic_ef_destroy(ef1);
    vic_ef_destroy(ef2);
    vic_ef_destroy(ef3);
    vic_ef_destroy(ef4);

    vic_destroy(vic1);
    vic_destroy(vic2);
    vic_destroy(vic3);
    vic_destroy(vic4);

    vic_ef_destroy(main_ef);
    vic_destroy(main_vic);

    return 0;
}