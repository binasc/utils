#ifndef __LIST_H__
#define __LIST_H__

#include <stdlib.h>

struct allocator_t
{
    void *(*alloc)(void *, size_t);
    void *(*realloc)(void *, void *, size_t);
    void (*free)(void *, void *);
    void *ctx;
}; 

struct duplicator_t
{
    void (*duplicate)(void *, void *, void *);
    void *ctx;
};

struct list_node_t;

struct list_t
{
    struct allocator_t alloc;
    struct duplicator_t duplicate;

    struct list_node_t *head;
    struct list_node_t *tail; /* always be a guard */

    size_t item_size;
};

struct list_iterator_t
{
    struct list_node_t *curr;
};

typedef int (*traverse_fn)(void *ctx, void *node);
typedef int (*remove_fn)(void *ctx, void *node);

struct list_t *list_create(size_t item_size,
                           struct duplicator_t *duplicate,
                           struct allocator_t *alloc);
void list_destroy(struct list_t *list);

int list_length(const struct list_t *list);
int list_empty(const struct list_t *list);

struct list_iterator_t list_begin(struct list_t *list);
struct list_iterator_t list_end(struct list_t *list);

int list_iterator_equal(struct list_iterator_t lhs,
                        struct list_iterator_t rhs);
struct list_iterator_t list_iterator_next(struct list_iterator_t it);
struct list_iterator_t list_iterator_prev(struct list_iterator_t it);

void *list_iterator_item(struct list_iterator_t it);

struct list_iterator_t list_insert(struct list_t *list,
                                   struct list_iterator_t pos, void *item);
struct list_iterator_t list_erase(struct list_t *list,
                                  struct list_iterator_t pos);

int list_remove_if(struct list_t *list, remove_fn rm, void *ctx);
void list_traverse(const struct list_t *list, traverse_fn t, void *ctx);
void list_rtraverse(const struct list_t *list, traverse_fn t, void *ctx);

void list_push_front(struct list_t *list, void *item);
void list_pop_front(struct list_t *list);
void list_push_back(struct list_t *list, void *item);
void list_pop_back(struct list_t *list);

void *list_front(struct list_t *list);
void *list_back(struct list_t *list);

#endif

