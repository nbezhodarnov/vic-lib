#ifndef PAUSE_THREAD_H
#define PAUSE_THREAD_H

#define _GNU_SOURCE
#include <pthread.h>

void pthread_pause_enable();
void pthread_pause_disable();

int pthread_pause(pthread_t thread);
int pthread_resume(pthread_t thread);

#endif
