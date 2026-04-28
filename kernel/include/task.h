#define TASK_PRIORITY_TIMER 1

typedef void (*task_callback_t)(void *args);

void add_task(task_callback_t callback, void *args, int priority);

void run_tasks();

