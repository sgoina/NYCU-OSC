#define TASK_RUNNING 0
#define TASK_ZOMBIE  1

struct task_struct {
    struct thread_struct {
        unsigned long ra;
        unsigned long sp;
        unsigned long s[12];
    } thread;
    int pid;
    int state;
    unsigned long kernel_sp;
    unsigned long user_sp;
    unsigned long stack;
    struct task_struct* next;
};

struct task_struct* get_current();
void schedule();
struct task_struct* thread_create(void (*threadfn)());
void thread_exit();
void kill_zombies();
void init_thread_queue();
void idle();
