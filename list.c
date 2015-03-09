#include "list.h"
#include <malloc.h>

/* guarded list algorithms, list setup responsibility
   is left to client */

/* allocate a list node */
node_t *list_node_alloc(){
  return (node_t *)malloc(sizeof(node_t));
}

/* free up a list node */
void list_node_free(node_t *node){
  free(node);
}

/* place data in first position of list */
void list_push(list_t *list, void *data){
  node_t *node = list_node_alloc();
  node->next = list->first->next;
  list->first->next = node;
}

/* remove the first element */
void *list_pop(list_t *list){
  node_t *node = list->first->next;
  void   *data = NULL;
  if (node != NULL) {
    data = node->data;
    list->first->next = node->next;
    list_node_free(node);
  }
  return data;
}

/* remove element data from list, assumes list is NULL 
   terminated */
void list_remove(list_t *list, void *data){
  node_t *last = list->first;
  node_t *it   = last->next;
  while (it != NULL) {
    if (data == it->data){
      last->next = it->next;
      list_node_free(it);
      return;
    }
    last = it;
    it = it->next;
  }
}
