#ifndef _LINKEDLIST_H
#define _LINKEDLIST_H

typedef struct _ll_Node {
  struct _ll_Node *next, *prev;
  void *item;
}ll_Node;

typedef struct _Linlist {
  ll_Node head;
  int count;
} Linlist;

ll_Node* new_ll_Node(void *);
Linlist* new_Linlist();

int ll_count(Linlist *);
void ll_insert_last(Linlist *, ll_Node *);
void ll_remove(Linlist *, ll_Node *);
ll_Node *ll_start(Linlist *);
ll_Node *ll_next(ll_Node *);
ll_Node *ll_end(Linlist *);

#endif // for #ifndef _LINKEDLIST_H