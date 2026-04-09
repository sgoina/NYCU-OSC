// basic list node structure
struct list_head {
    struct list_head *next, *prev;
};

static inline void INIT_LIST_HEAD(struct list_head *list) {
    list->next = list;
    list->prev = list;
}

// Add a new node into list
static inline void __list_add(struct list_head *new_node,
                              struct list_head *prev,
                              struct list_head *next) {
    next->prev = new_node;
    new_node->next = next;
    new_node->prev = prev;
    prev->next = new_node;
}

// Add a new node to the head of list
static inline void list_add_front(struct list_head *new_node, struct list_head *head) {
    __list_add(new_node, head, head->next);
}

// Add a new node to the tail of list
static inline void list_add_back(struct list_head *new_node, struct list_head *head) {
    __list_add(new_node, head->prev, head);
}

// delete a node
static inline void __list_del(struct list_head *prev, struct list_head *next) {
    next->prev = prev;
    prev->next = next;
}

// delete a chosen node
static inline void list_remove(struct list_head *entry) {
    __list_del(entry->prev, entry->next);
    entry->next = NULL;
    entry->prev = NULL;
}

// show whether the list is empty
static inline int list_empty(const struct list_head *head) {
    return head->next == head;
}

// get the first element in the list
static inline struct list_head *list_front(struct list_head *head) {
    return head->next;
}

// get the size of the list
static inline int list_size(const struct list_head *head) {
    int count = 0;
    struct list_head *curr = head->next;
    while (curr != head) {
        count++;
        curr = curr->next;
    } 
    return count;
}
