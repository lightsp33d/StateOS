#include "test.h"

void test_mutex()
{
	TEST_Notify();
	TEST_Add(test_mutex_0);
	TEST_Add(test_mutex_1);
	TEST_Add(test_mutex_2);
	TEST_Add(test_mutex_3);
	TEST_Add(test_mutex_4);
	TEST_Add(test_mutex_5);
}
