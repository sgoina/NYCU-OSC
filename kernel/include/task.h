#ifndef __TASK_H__
#define __TASK_H__

#include "list.h"

typedef void (*task_callback_t)(char *arg);

struct task_event {
    struct list_head list;
    int priority;
    task_callback_t callback;
    void *arg;
};

void task_init();
void add_task(task_callback_t callback, char *arg, int priority);
void run_tasks();

#endif
