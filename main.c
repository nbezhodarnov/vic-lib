#include <stdio.h>
#include "ef_lib.h"

// You can create and link other execution flows from within an execution flow
// So basically, it's like a nesting, because this routine can be called from a thread or a process

// P.S. I'm not sure if this is the best way to acheive nesting,
// I think it won't work if this routine will be executed in a process and we will want to create another execution flow
// from within this routine and we will want to link it to any of execution flows of the parent process
// So, I think we should determine the hole configuration of the execution flows before starting them
// in the main function
void routine1(ef_t *ef) {
    ef_t *ef1 = ef_create(routine1, EF_PROCESS, NULL);

    ef_link(ef, ef1);

    ef_start(ef1);

    // Do some work in the current execution flow

    ef_wait(ef1);
};

void routine2(ef_t *ef) {

    ef_t *ef1 = ef_create(routine1, EF_THREAD | EF_PROCESS, NULL);

    ef_link(ef, ef1);

    ef_start(ef1);

    // Do some work in the current execution flow

    ef_wait(ef1);
};

void routine3(ef_t *ef) {
    //...
};

int main() {
    ef_t *main_ef = ef_init();

    ef_t *ef1 = ef_create(routine1, EF_THREAD, NULL);
    ef_t *ef2 = ef_create(routine2, EF_THREAD | EF_PROCESS, NULL);

    // ef1 and ef2 now are linked which allows them to communicate with each other
    ef_link(ef1, ef2);

    ef_start(ef1);
    ef_start(ef2);

    ef_wait(ef1);
    ef_wait(ef2);

    return 0;
}