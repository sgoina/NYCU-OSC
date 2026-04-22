#define BUF_SIZE 2048

typedef struct {
    char buffer[BUF_SIZE];
    volatile int head;
    volatile int tail;
} RingBuffer;

static inline int is_empty(RingBuffer *rb) { return rb->head == rb->tail; }
static inline int is_full(RingBuffer *rb)  { return ((rb->tail + 1) % BUF_SIZE) == rb->head; }

static inline void push(RingBuffer *rb, char c) {
    if (!is_full(rb)) {
        rb->buffer[rb->tail] = c;
        rb->tail = (rb->tail + 1) % BUF_SIZE;
    }
}

static inline char pop(RingBuffer *rb) {
    char c = 0;
    if (!is_empty(rb)) {
        c = rb->buffer[rb->head];
        rb->head = (rb->head + 1) % BUF_SIZE;
    }
    return c;
}
