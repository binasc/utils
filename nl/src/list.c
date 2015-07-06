#include "list.h"
#include <string.h>

static void *def_alloc(void *ctx, size_t size)
{ return malloc(size); }
static void *def_realloc(void *ctx, void *ptr, size_t size)
{ return realloc(ptr, size); }
static void def_free(void *ctx, void *size)
{ free(size); }

static void def_duplicate(void *ctx, void *dst, void *src)
{ memcpy(dst, src, (size_t)ctx); }

static struct allocator_t def_allocator = {
    def_alloc, def_realloc, def_free, NULL
};

struct list_node_t {
    struct list_node_t *prev;
    struct list_node_t *next;
    char item[];
};

struct list_t *list_create(size_t item_size,
                           struct duplicator_t *duplicate,
                           struct allocator_t *alloc)
{
    struct list_t *list;

    if (alloc == NULL) {
        alloc = &def_allocator;
    }

    list = alloc->alloc(alloc->ctx, sizeof(struct list_t));
    if (list != NULL) {
        if (duplicate == NULL) {
            list->duplicate.duplicate = def_duplicate;
            list->duplicate.ctx = (void *)item_size;
        }
        else {
            list->duplicate = *duplicate;
        }
        list->alloc = *alloc;
        list->head = alloc->alloc(alloc->ctx, sizeof(struct list_node_t));
        if (list->head == NULL) {
            alloc->free(alloc->ctx, list);
            return NULL;
        }
        list->head->prev = NULL;
        list->head->next = NULL;
        list->tail = list->head;
        list->item_size = item_size;
    }
    return list;
}

static int remove_all(void *ctx, void *node)
{
    return 1; /* include guard */
}

void list_destroy(struct list_t *list)
{
    list_remove_if(list, remove_all, NULL);
    list->alloc.free(list->alloc.ctx, list);
}

int list_length(const struct list_t *list)
{
    int len = 0;
    struct list_node_t *node;

    for (node = list->head; node; node = node->next) {
        ++len;
    }

    return len - 1;
}

int list_empty(const struct list_t *list)
{
    return list->head == list->tail ? 1 : 0;
}

int list_iterator_equal(struct list_iterator_t lhs,
                        struct list_iterator_t rhs)
{
    return lhs.curr == rhs.curr ? 1 : 0;
}

struct list_iterator_t list_begin(struct list_t *list)
{
    struct list_iterator_t it = { list->head };
    return it;
}

struct list_iterator_t list_end(struct list_t *list)
{
    struct list_iterator_t it = { list->tail };
    return it;
}

struct list_iterator_t list_iterator_next(struct list_iterator_t it)
{
    it.curr = it.curr->next;
    return it;
}

struct list_iterator_t list_iterator_prev(struct list_iterator_t it)
{
    it.curr = it.curr->prev;
    return it;
}

void *list_iterator_item(struct list_iterator_t it)
{
    return it.curr->item;
}

struct list_iterator_t list_insert(struct list_t *list,
                                   struct list_iterator_t pos, void *item)
{
    struct list_iterator_t rc;
    struct list_node_t *node;

    node = list->alloc.alloc(list->alloc.ctx,
                             sizeof(struct list_node_t) + list->item_size);
    if (node == NULL) {
        return list_end(list);
    }
    list->duplicate.duplicate(list->duplicate.ctx, node->item, item);

    node->prev = pos.curr->prev;
    if (pos.curr->prev == NULL) {
        list->head = node;
    }
    else {
        pos.curr->prev->next = node;
    }
    node->next = pos.curr;
    pos.curr->prev = node;

    rc.curr = node;
    return rc;
}

struct list_iterator_t list_erase(struct list_t *list,
                                  struct list_iterator_t pos)
{
    struct list_iterator_t it;

    if (pos.curr->prev == NULL) {
        list->head = pos.curr->next;
    }
    else {
        pos.curr->prev->next = pos.curr->next;
    }
    pos.curr->next->prev = pos.curr->prev;

    it.curr = pos.curr->next;
    list->alloc.free(list->alloc.ctx, pos.curr);
    return it;
}

int list_remove_if(struct list_t *list, remove_fn rm, void *ctx)
{
    int ret, i = 0;
    struct list_node_t **curr, *entry;

    for (curr = &list->head; *curr;) {
        entry = *curr;
        if ((ret = rm(ctx, entry->item)) > 0) {
            *curr = entry->next;
            if (*curr) {
                (*curr)->prev = entry->prev;
            }
            else {
                list->tail = entry->prev;
            }
            list->alloc.free(list->alloc.ctx, entry);
            ++i;
        }
        else if (ret == 0) {
            curr = &(*curr)->next;
        }
        else {
            break;
        }
    }

    return i;
}

void list_traverse(const struct list_t *list, traverse_fn t, void *ctx)
{
    const struct list_node_t *node;

    for (node = list->head; node; node = node->next) {
        if (t(ctx, (void *)node->item) == 0) {
            break;
        }
    }
}

void list_rtraverse(const struct list_t *list, traverse_fn t, void *ctx)
{
    const struct list_node_t *node;

    for (node = list->tail; node; node = node->prev) {
        if (t(ctx, (void *)node->item) == 0) {
            break;
        }
    }
}

void list_push_front(struct list_t *list, void *item)
{
    list_insert(list, list_begin(list), item);
}

void list_pop_front(struct list_t *list)
{
    list_erase(list, list_begin(list));
}

void list_push_back(struct list_t *list, void *item)
{
    list_insert(list, list_end(list), item);
}

void list_pop_back(struct list_t *list)
{
    list_erase(list, list_iterator_prev(list_end(list)));
}

void *list_front(struct list_t *list)
{
    return list_iterator_item(list_begin(list));
}

void *list_back(struct list_t *list)
{
    return list_iterator_item(list_iterator_prev(list_end(list)));
}

