#ifndef _LIST_H_
#define _LIST_H_

typedef struct list_s list_t;
typedef struct circular_s circular_t;

list_t* list_init(void);
void    list_push(list_t* list, void* data);
void*   list_top(list_t* list);
void*   list_pop(list_t* list);
void    list_remove(list_t* list, void* data);
int     list_mem(list_t* list, void* data);
int     list_length(list_t *list);

circular_t* circular_init(void);
void        circular_push(circular_t* list, void* data);
void*       circular_top(circular_t* list);
void*       circular_pop(circular_t* list);
void        circular_remove(circular_t* list, void* data);
int         circular_mem(circular_t* list, void* data);
int         circular_length(circular_t *list);
void        circular_rotate(circular_t *list);
#endif /* _LIST_H_ */
