#include <stm32f4_discovery.h>
#include <os.h>
#include <stdio.h>
#include "test_resources.h"

#pragma once

#define PASS                   1000

#ifndef DEBUG
#define ASSERT(event)          (void)(event)
#else
#define ASSERT(event)          assert(event)
#endif

#define ASSERT_success(event)  ASSERT((event) == E_SUCCESS)
#define ASSERT_failure(event)  ASSERT((event) == E_FAILURE)
#define ASSERT_stopped(event)  ASSERT((event) == E_STOPPED)
#define ASSERT_deleted(event)  ASSERT((event) == E_DELETED)
#define ASSERT_timeout(event)  ASSERT((event) == E_TIMEOUT)
#define ASSERT_owndead(event)  ASSERT((event) == OWNERDEAD)

#define ASSERT_dead(tsk)       ASSERT((tsk)->hdr.id == ID_STOPPED)
#define ASSERT_ready(tsk)      ASSERT((tsk)->hdr.id == ID_READY)

#ifdef  __cplusplus
extern "C" {
#endif

void test_add (fun_t *fun);
void test_call(fun_t *fun);

#ifdef  __cplusplus
}
#endif

#define TEST_Add(fun)          do { void fun (void); test_add(fun); } while (0)
#define TEST_AddUnit(unit)     do { void unit(void); unit();        } while (0)
#define TEST_Call()            do { test_call(test);                } while (0)

#ifdef  DEBUG
#ifdef  __CSMC__
#define UNIT_Notify()          puts(__FILE__)
#define TEST_Notify()          puts(__FILE__)
#else//!__CSMC__
#define UNIT_Notify()          puts(__FUNCTION__)
#define TEST_Notify()          puts(__FUNCTION__)
#endif//__CSMC__
#else//!DEBUG
#define UNIT_Notify()          LED_Tick()
#define TEST_Notify()          LED_Tick()
#endif//DEBUG
