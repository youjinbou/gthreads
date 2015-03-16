#include "list.h"
#include "debug.h"
#include <malloc.h>
#include <stdlib.h>

/*
#undef DBG
#undef DBG_FUN

#define DBG(z,...)
#define DBG_FUN(a)
*/

typedef struct node_s node_t;

struct node_s {
  node_t* next;
  void*   data;
};

struct list_s {
  node_t* first;
};

struct circular_s {
  node_t* first;
  node_t* last;
};

static void* alloc(size_t size){
  return calloc(1, size);
}

/* allocate a list node */
node_t* list_node_alloc(){
  node_t* node = (node_t* )alloc(sizeof(node_t));
  if (!node){
    perror("list_node_alloc");
    exit(EXIT_FAILURE);
  }
  return node;
}

/* free up a list node */
void list_node_free(node_t* node){
  free(node);
}


list_t *list_init(){
  list_t *l = (list_t*)alloc(sizeof(list_t));
  l->first = NULL;
  return l;
}

int list_empty(list_t* list){
  return list->first == NULL;
}

/* place data in first position of list */
void list_push(list_t* list, void* data){
  DBG("%s(%p, %p)\n", __FUNCTION__, list, data);
  node_t* node = list_node_alloc();
  node->next = list->first;
  list->first = node;
  node->data = data;
  DBG_FUN("end");
}

/* remove the first element */
void* list_pop(list_t* list){
  DBG("%s(%p)\n", __FUNCTION__, list);
  if (list_empty(list)) 
    return NULL;
  node_t* node = list->first;
  void  * data = node->data;
  list->first = node->next;
  list_node_free(node);
  DBG_FUN("end");
  return data;
}

void* list_top(list_t* list){
  if (!list_empty(list))
    return list->first->data;
  return NULL;
}

/* remove element data from list, assumes list is NULL terminated */
void list_remove(list_t* list, void* data){
  if (list_empty(list))
    return;
  node_t* last = list->first;
  node_t* it   = last->next;
  if (it){
    while (it != NULL){
      if (data == it->data){
        last->next = it->next;
        list_node_free(it);
        return;
      }
      last = it;
      it = it->next;
    }
  }
  else {
    if (data == list->first->data){
      list_node_free(list->first);
      list->first = NULL;
    }
  }
}

int list_mem(list_t* list, void* data){
  node_t* it = list->first;
  while (it != NULL) {
    if (data == it->data){
      return 1;
    }
    it = it->next;
  }
  return 0;
}

int list_length(list_t* list){
  int i = 0;
  node_t* it = list->first;
  while (it != NULL) {
    i++;
    it = it->next;
  }
  return i;
}

/* circular list primitives -------------- */

circular_t* circular_init(){
  circular_t* list = (circular_t*)alloc(sizeof(circular_t));
  return list;
}

void circular_push(circular_t* list, void* data){
  if (list->first){
    node_t* first = list_node_alloc();
    first->data = (void* )data;
    first->next = list->first;
    list->last->next = first;
    list->first = first;
  } else {
    node_t* first = list_node_alloc();
    first->data = data;
    first->next = first;
    list->first = first;
    list->last  = first;
  }
}

void* circular_top(circular_t* list){
  if (list->first){
    return list->first->data;
  }
  return NULL;
}

void* circular_pop(circular_t* list){
  void* data = NULL;
  node_t* first = list->first;;
  data = first->data;
  if (list->first != list->last) {
    list->last->next = first->next;
  } else {
    list->first = list->last = NULL;
  }
  list_node_free(first);
  return data;
}

void circular_rotate(circular_t* list){
  list->last = list->first;
  list->first = list->last->next;
}

void circular_remove(circular_t* list, void* ptr){
  DBG_FUN("start");
  node_t* it   = list->first;
  node_t* prev = list->last;
  if (it == prev){
    list_node_free(it);
    list->first = list->last = NULL;
    return;
  }
  do {
    if (ptr == it->data){
      DBG("dropping %p\n", ptr);
      prev->next = it->next;
      list_node_free(it);
      return;
    }
    prev = it;
    it   = it->next;
  } while (it != list->first);
  DBG_FUN("end");
}

int circular_mem(circular_t* list, void* data){
  node_t* first = list->first;
  if (!first)
    return 0;
  do {
    if (data == first->data)
      return 1;
    first = first->next;
  } while (first != list->first);
  return 0;
}

int circular_length(circular_t* list){
  int i = 1;
  node_t* first = list->first;
  do {
    i++;
  } while (first != list->first);
  return i;
}
