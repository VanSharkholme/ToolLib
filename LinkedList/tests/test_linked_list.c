#include "linked_list.h"
#include "test_runner.h"

static SLinkedListNode head, a, b, c, outsider;

void setUp(void)
{
    memset(&head, 0, sizeof head);
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    memset(&c, 0, sizeof c);
    memset(&outsider, 0, sizeof outsider);
}

void tearDown(void) {}

/* Build fixtures independently of InsertAt so failures remain localized. */
static void three_nodes(void)
{
    head.next = &a;
    a.next = &b;
    b.next = &c;
}

static void test_init_clears_head(void)
{
    head.next = &a;
    SLinkedList_Init(&head);
    TEST_ASSERT_NULL(head.next);
    TEST_ASSERT_TRUE(SLinkedList_IsEmpty(&head));
    TEST_ASSERT_EQUAL_UINT32(0, SLinkedList_Length(&head));
}

static void test_null_arguments_are_safe(void)
{
    SLinkedList_Init(NULL);
    TEST_ASSERT_TRUE(SLinkedList_IsEmpty(NULL));
    TEST_ASSERT_EQUAL_UINT32(0, SLinkedList_Length(NULL));
    TEST_ASSERT_NULL(SLinkedList_Pop(NULL));
    SLinkedList_InsertAt(NULL, &a);
    SLinkedList_InsertAt(&head, NULL);
    SLinkedList_Remove(NULL, &a);
    SLinkedList_Remove(&head, NULL);
    TEST_ASSERT_NULL(head.next);
    TEST_ASSERT_NULL(a.next);
}

static void test_insert_preserves_successor(void)
{
    SLinkedList_InsertAt(&head, &a);
    SLinkedList_InsertAt(&head, &b);
    SLinkedList_InsertAt(&a, &c);
    TEST_ASSERT_EQUAL_PTR(&b, head.next);
    TEST_ASSERT_EQUAL_PTR(&a, b.next);
    TEST_ASSERT_EQUAL_PTR(&c, a.next);
    TEST_ASSERT_NULL(c.next);
    TEST_ASSERT_FALSE(SLinkedList_IsEmpty(&head));
    TEST_ASSERT_EQUAL_UINT32(3, SLinkedList_Length(&head));
}

static void test_length_counts_nodes_not_sentinel(void)
{
    three_nodes();
    TEST_ASSERT_EQUAL_UINT32(3, SLinkedList_Length(&head));
    TEST_ASSERT_EQUAL_UINT32(2, SLinkedList_Length(&a));
    TEST_ASSERT_EQUAL_UINT32(0, SLinkedList_Length(&c));
}

static void test_pop_returns_nodes_in_order(void)
{
    three_nodes();
    TEST_ASSERT_EQUAL_PTR(&a, SLinkedList_Pop(&head));
    TEST_ASSERT_EQUAL_PTR(&b, head.next);
    TEST_ASSERT_EQUAL_PTR(&b, SLinkedList_Pop(&head));
    TEST_ASSERT_EQUAL_PTR(&c, SLinkedList_Pop(&head));
    TEST_ASSERT_NULL(head.next);
    TEST_ASSERT_NULL(SLinkedList_Pop(&head));
}

static void test_remove_first_node(void)
{
    three_nodes();
    SLinkedList_Remove(&head, &a);
    TEST_ASSERT_EQUAL_PTR(&b, head.next);
    TEST_ASSERT_EQUAL_PTR(&c, b.next);
    TEST_ASSERT_NULL(c.next);
}

static void test_remove_only_node(void)
{
    head.next = &a;
    SLinkedList_Remove(&head, &a);
    TEST_ASSERT_NULL(head.next);
}

static void test_remove_middle_node(void)
{
    three_nodes();
    SLinkedList_Remove(&head, &b);
    TEST_ASSERT_EQUAL_PTR(&a, head.next);
    TEST_ASSERT_EQUAL_PTR(&c, a.next);
    TEST_ASSERT_NULL(c.next);
}

static void test_remove_last_node(void)
{
    three_nodes();
    SLinkedList_Remove(&head, &c);
    TEST_ASSERT_EQUAL_PTR(&a, head.next);
    TEST_ASSERT_EQUAL_PTR(&b, a.next);
    TEST_ASSERT_NULL(b.next);
}

static void test_remove_absent_node_preserves_list(void)
{
    three_nodes();
    SLinkedList_Remove(&head, &outsider);
    TEST_ASSERT_EQUAL_PTR(&a, head.next);
    TEST_ASSERT_EQUAL_PTR(&b, a.next);
    TEST_ASSERT_EQUAL_PTR(&c, b.next);
    TEST_ASSERT_NULL(c.next);
}

static void test_remove_from_empty_list(void)
{
    SLinkedList_Remove(&head, &a);
    TEST_ASSERT_NULL(head.next);
}

static void test_entry_macros_recover_containing_object(void)
{
    struct Item { int value; SLinkedListNode node; } item = {42, {NULL}};
    head.next = &item.node;
    TEST_ASSERT_EQUAL_PTR(&item, SLinkedList_Entry(&item.node, struct Item, node));
    TEST_ASSERT_EQUAL_PTR(&item, SLinkedList_First_Entry(&head, struct Item, node));
    TEST_ASSERT_EQUAL_INT(42, SLinkedList_First_Entry(&head, struct Item, node)->value);
}

int main(int argc, char **argv)
{
    const TestCase cases[] = {
        TEST_CASE(test_init_clears_head),
        TEST_CASE(test_null_arguments_are_safe),
        TEST_CASE(test_insert_preserves_successor),
        TEST_CASE(test_length_counts_nodes_not_sentinel),
        TEST_CASE(test_pop_returns_nodes_in_order),
        TEST_CASE(test_remove_first_node),
        TEST_CASE(test_remove_only_node),
        TEST_CASE(test_remove_middle_node),
        TEST_CASE(test_remove_last_node),
        TEST_CASE(test_remove_absent_node_preserves_list),
        TEST_CASE(test_remove_from_empty_list),
        TEST_CASE(test_entry_macros_recover_containing_object),
    };
    return run_test_cases(argc, argv, __FILE__, cases, sizeof cases / sizeof cases[0]);
}
