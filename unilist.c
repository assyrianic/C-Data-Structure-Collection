
#include <stdlib.h>
#include <stdio.h>
#include "dsc.h"

/* Singly Linked List Node code */
/////////////////////////////////////////
struct UniListNode *UniListNode_New(void)
{
	return calloc(1, sizeof(struct UniListNode));
}

struct UniListNode *UniListNode_NewVal(const union Value val)
{
	struct UniListNode *node = calloc(1, sizeof *node);
	if( node )
		node->Data = val;
	return node;
}

void UniListNode_Del(struct UniListNode *const __restrict node, bool (*dtor)())
{
	if( !node )
		return;
	
	if( dtor )
		(*dtor)(&node->Data.Ptr);
	
	if( node->Next )
		UniListNode_Free(&node->Next, dtor);
}

void UniListNode_Free(struct UniListNode **__restrict node, bool (*dtor)())
{
	if( !*node )
		return;
	
	UniListNode_Del(*node, dtor);
	free(*node); *node=NULL;
}

struct UniListNode *UniListNode_GetNextNode(const struct UniListNode *const __restrict node)
{
	return node ? node->Next : NULL;
}
union Value UniListNode_GetValue(const struct UniListNode *const __restrict node)
{
	return node ? node->Data : (union Value){0};
}
/////////////////////////////////////////


/* Singly Linked List code */
/////////////////////////////////////////
struct UniLinkedList *UniLinkedList_New(bool (*dtor)())
{
	struct UniLinkedList *list = calloc(1, sizeof *list);
	UniLinkedList_SetDestructor(list, dtor);
	return list;
}

void UniLinkedList_Del(struct UniLinkedList *const __restrict list)
{
	if( !list )
		return;
	
	UniListNode_Free(&list->Head, list->Destructor);
	*list = (struct UniLinkedList){0};
}

void UniLinkedList_Free(struct UniLinkedList **__restrict list)
{
	if( !*list )
		return;
	
	UniLinkedList_Del(*list);
	free(*list), *list=NULL;
}

void UniLinkedList_Init(struct UniLinkedList *const __restrict list, bool (*dtor)())
{
	if( !list )
		return;
	
	*list = (struct UniLinkedList){0};
	UniLinkedList_SetDestructor(list, dtor);
}

size_t UniLinkedList_Len(const struct UniLinkedList *const __restrict list)
{
	return list ? list->Len : 0;
}

bool UniLinkedList_InsertNodeAtHead(struct UniLinkedList *const __restrict list, struct UniListNode *const __restrict node)
{
	if( !list or !node )
		return false;
	
	node->Next = list->Head;
	list->Head = node;
	if( !list->Len )
		list->Tail = node;
	list->Len++;
	return true;
}

bool UniLinkedList_InsertNodeAtTail(struct UniLinkedList *const __restrict list, struct UniListNode *const __restrict node)
{
	if( !list or !node )
		return false;
	
	if( list->Len ) {
		node->Next = NULL;
		list->Tail->Next = node;
		list->Tail = node;
	}
	else list->Head = list->Tail = node;
	list->Len++;
	return true;
}

bool UniLinkedList_InsertNodeAtIndex(struct UniLinkedList *const __restrict list, struct UniListNode *const __restrict node, const size_t index)
{
	if( !list or !node )
		return false;
	else if( !list->Head or index==0 )
		return UniLinkedList_InsertNodeAtHead(list, node);
	// if index is out of bounds, append at tail end.
	else if( index >= list->Len )
		return UniLinkedList_InsertNodeAtTail(list, node);
	
	struct UniListNode
		*curr=list->Head,
		*prev=NULL
	;
	size_t i=0;
	while( curr->Next != NULL and i != index ) {
		prev = curr;
		curr = curr->Next;
		i++;
	}
	if( i ) {
		if( prev == list->Tail )
			list->Tail->Next = node;
		else prev->Next = node;
		node->Next = curr;
		list->Len++;
		return true;
	}
	return false;
}

bool UniLinkedList_InsertValueAtHead(struct UniLinkedList *const __restrict list, const union Value val)
{
	if( !list )
		return false;
	
	return UniLinkedList_InsertNodeAtHead(list, UniListNode_NewVal(val));
}

bool UniLinkedList_InsertValueAtTail(struct UniLinkedList *const __restrict list, const union Value val)
{
	if( !list )
		return false;
	
	return UniLinkedList_InsertNodeAtTail(list, UniListNode_NewVal(val));
}

bool UniLinkedList_InsertValueAtIndex(struct UniLinkedList *const __restrict list, const union Value val, const size_t index)
{
	if( !list )
		return false;
	
	return UniLinkedList_InsertNodeAtIndex(list, UniListNode_NewVal(val), index);
}

struct UniListNode *UniLinkedList_GetNode(const struct UniLinkedList *const __restrict list, const size_t index)
{
	if( !list )
		return NULL;
	else if( index >= list->Len )
		return list->Tail;
	
	struct UniListNode *node = list->Head;
	for( size_t i=0 ; i<list->Len ; i++ ) {
		if( node and i==index )
			return node;
		node = node->Next;
	}
	return NULL;
}

union Value UniLinkedList_GetValue(const struct UniLinkedList *const __restrict list, const size_t index)
{
	if( !list )
		return (union Value){0};
	else if( index >= list->Len )
		return list->Tail->Data;
	
	struct UniListNode *node = list->Head;
	for( size_t i=0 ; i<list->Len ; i++ ) {
		if( node and i==index )
			return node->Data;
		node = node->Next;
	}
	return (union Value){0};
}

void UniLinkedList_SetValue(struct UniLinkedList *const __restrict list, const size_t index, const union Value val)
{
	if( !list )
		return;
	else if( index >= list->Len ) {
		list->Tail->Data = val;
		return;
	}
	
	struct UniListNode *node = list->Head;
	for( size_t i=0 ; i<list->Len ; i++ ) {
		if( node and i==index ) {
			node->Data = val;
			break;
		}
		node = node->Next;
	}
}

bool UniLinkedList_DelNodeByIndex(struct UniLinkedList *const __restrict list, const size_t index)
{
	if( !list or !list->Len )
		return false;
	
	struct UniListNode *node = UniLinkedList_GetNode(list, index);
	if( !node )
		return false;
	
	if( node==list->Head )
		list->Head = node->Next;
	else {
		struct UniListNode *travnode = list->Head;
		for( size_t i=0 ; i<list->Len ; i++ ) {
			if( travnode->Next == node ) {
				if( list->Tail == node ) {
					travnode->Next = NULL;
					list->Tail = travnode;
				}
				else travnode->Next = node->Next;
				break;
			}
			travnode = travnode->Next;
		}
	}
	
	if( list->Destructor )
		(*list->Destructor)(&node->Data.Ptr);
	free(node), node=NULL;
	
	list->Len--;
	if( !list->Len )
		list->Tail = NULL;
	return true;
}

bool UniLinkedList_DelNodeByRef(struct UniLinkedList *const __restrict list, struct UniListNode **__restrict noderef)
{
	if( !list or !*noderef )
		return false;
	
	struct UniListNode *node = *noderef;
	if( node==list->Head )
		list->Head = node->Next;
	else {
		struct UniListNode *travnode = list->Head;
		for( size_t i=0 ; i<list->Len ; i++ ) {
			if( travnode->Next == node ) {
				if( list->Tail == node ) {
					travnode->Next = NULL;
					list->Tail = travnode;
				}
				else travnode->Next = node->Next;
				break;
			}
			travnode = travnode->Next;
		}
	}
	
	if( list->Destructor )
		(*list->Destructor)(&node->Data.Ptr);
	
	free(*noderef), *noderef=NULL;
	list->Len--;
	return true;
}

void UniLinkedList_SetDestructor(struct UniLinkedList *const __restrict list, bool (*dtor)())
{
	if( !list )
		return;
	
	list->Destructor = dtor;
}

struct UniListNode *UniLinkedList_GetHead(const struct UniLinkedList *const __restrict list)
{
	return list ? list->Head : NULL;
}
struct UniListNode *UniLinkedList_GetTail(const struct UniLinkedList *const __restrict list)
{
	return list ? list->Tail : NULL;
}