//
// Created by VanSharkholme on 2026/9/6.
//

#ifndef LINKED_LIST_H
#define LINKED_LIST_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define CONTAINER_OF(ptr, type, member) \
    ((type *)((char *)ptr - (size_t)(&((type *)0)->member)))

typedef struct SingleLinkedListNode
{
    struct SingleLinkedListNode* next;
} SLinkedListNode;

typedef SLinkedListNode* SLinkedList;

#define SLinkedList_Entry(node, type, member) \
    CONTAINER_OF(node, type, member)
#define SLinkedList_First_Entry(list, type, member) \
    SLinkedList_Entry((list)->next, type, member)

static inline void SLinkedList_Init(SLinkedList list)
{
    if (list == NULL)
    {
        return;
    }
    list->next = NULL;
}

static inline uint32_t SLinkedList_Length(SLinkedList list)
{
    if (list == NULL)
    {
        return 0;
    }
    uint32_t length = 0;
    const SLinkedListNode *current = list->next;
    while (current != NULL)
    {
        length++;
        current = current->next;
    }
    return length;
}

static inline bool SLinkedList_IsEmpty(SLinkedList list)
{
    if (list == NULL)
    {
        return true;
    }
    return list->next == NULL;
}

static inline void SLinkedList_InsertAt(SLinkedListNode* target, SLinkedListNode* new_node)
{
    if (target == NULL || new_node == NULL)
    {
        return;
    }
    new_node->next = target->next;
    target->next = new_node;
}

static inline void SLinkedList_Remove(SLinkedList list, SLinkedListNode *node)
{
    if (list == NULL || node == NULL)
    {
        return;
    }
    SLinkedListNode *prev = list;
    while (prev != NULL && prev->next != node)
    {
        prev = prev->next;
    }
    if (prev != NULL)
    {
        prev->next = node->next;
    }
}

static inline SLinkedListNode* SLinkedList_Pop(SLinkedList list)
{
    if (list == NULL || list->next == NULL)
    {
        return NULL;
    }
    SLinkedListNode *node = list->next;
    list->next = node->next;
    return node;
}

#endif //LINKED_LIST_H
