#include <stdio.h>
#include "../list.h"

void traverse_list(struct list_t *list)
{
    int val;
    struct list_iterator_t it;
    for (it = list_begin(list);
         !list_iterator_equal(it, list_end(list));
         it = list_iterator_next(it))
    {
        val = *(int *)list_iterator_item(it);
        printf("%d ", val);
    }
    puts("");
}

int main(int argc, char *argv[])
{
    int val;
    struct list_t *list;

    list = list_create(sizeof(int), NULL, NULL);

    // push/pop front/back for empty list
    val = 1;
    list_push_front(list, &val);
    traverse_list(list); // 1

    list_pop_front(list);
    traverse_list(list); //

    list_push_back(list, &val);
    traverse_list(list); // 1

    list_pop_back(list);
    traverse_list(list); //

    val = 2;
    list_push_front(list, &val);
    // push front/back for 1-length list
    val = 1;
    list_push_front(list, &val);
    traverse_list(list); // 1 2

    list_pop_back(list);
    val = 2;
    list_push_back(list, &val);
    traverse_list(list); // 1 2

    // test for other ops
    val = *(int *)list_front(list);
    printf("%d\n", val); // 1

    val = *(int *)list_back(list);
    printf("%d\n", val); // 2

    list_destroy(list);

    // list_iterator_equal
    // list_length
    // list_empty
    // list_remove_if
    // list_traverse
    // list_rtraverse

    return 0;
}

