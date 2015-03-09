#ifndef _LIST_H_
#define _LIST_H_

typedef struct node_s node_t;

struct node_s {
  node_t *next;
  void   *data;
};

typedef struct list_s list_t;

struct list_s {
  node_t *first;
};

node_t *list_node_alloc();
void    list_node_free(node_t *node);
void    list_push(list_t *list, void *data);
void   *list_pop(list_t *list);
void    list_remove(list_t *list, void *data);

#endif /* _LIST_H_ */
