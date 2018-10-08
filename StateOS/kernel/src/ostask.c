/******************************************************************************

    @file    StateOS: ostask.c
    @author  Rajmund Szymanski
    @date    08.10.2018
    @brief   This file provides set of functions for StateOS.

 ******************************************************************************

   Copyright (c) 2018 Rajmund Szymanski. All rights reserved.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to
   deal in the Software without restriction, including without limitation the
   rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
   sell copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included
   in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
   IN THE SOFTWARE.

 ******************************************************************************/

#include "inc/ostask.h"
#include "inc/oscriticalsection.h"
#include "osalloc.h"

/* -------------------------------------------------------------------------- */
void tsk_init( tsk_t *tsk, unsigned prio, fun_t *state, stk_t *stack, unsigned size )
/* -------------------------------------------------------------------------- */
{
	assert_tsk_context();
	assert(tsk);
	assert(state);
	assert(stack);
	assert(size);

	sys_lock();
	{
		memset(tsk, 0, sizeof(tsk_t));

		core_hdr_init(&tsk->hdr);

		tsk->prio  = prio;
		tsk->basic = prio;
		tsk->state = state;
		tsk->stack = stack;
		tsk->size  = size;

		core_ctx_init(tsk);
		core_tsk_insert(tsk);
	}
	sys_unlock();
}

/* -------------------------------------------------------------------------- */
tsk_t *wrk_create( unsigned prio, fun_t *state, unsigned size )
/* -------------------------------------------------------------------------- */
{
	tsk_t *tsk;

	assert_tsk_context();
	assert(state);
	assert(size);

	sys_lock();
	{
		tsk = sys_alloc(SEG_OVER(sizeof(tsk_t)) + size);
		tsk_init(tsk, prio, state, (void *)((size_t)tsk + SEG_OVER(sizeof(tsk_t))), size);
		tsk->hdr.obj.res = tsk;
	}
	sys_unlock();

	return tsk;
}

/* -------------------------------------------------------------------------- */
tsk_t *wrk_detached( unsigned prio, fun_t *state, unsigned size )
/* -------------------------------------------------------------------------- */
{
	tsk_t *tsk;

	assert_tsk_context();
	assert(state);
	assert(size);

	sys_lock();
	{
		tsk = sys_alloc(SEG_OVER(sizeof(tsk_t)) + size);
		tsk_init(tsk, prio, state, (void *)((size_t)tsk + SEG_OVER(sizeof(tsk_t))), size);
		tsk->hdr.obj.res = tsk;
		tsk->join = DETACHED;
	}
	sys_unlock();

	return tsk;
}

/* -------------------------------------------------------------------------- */
void tsk_start( tsk_t *tsk )
/* -------------------------------------------------------------------------- */
{
	assert_tsk_context();
	assert(tsk);
	assert(tsk->hdr.obj.res!=RELEASED);  // object with released resources cannot be used
	assert(tsk->state);

	sys_lock();
	{
		if (tsk->hdr.id == ID_STOPPED && // task is already active
		    tsk->join != DETACHED)       // detached task cannot be (re)started
		{
			core_ctx_init(tsk);
			core_tsk_insert(tsk);
		}
	}
	sys_unlock();
}

/* -------------------------------------------------------------------------- */
void tsk_startFrom( tsk_t *tsk, fun_t *state )
/* -------------------------------------------------------------------------- */
{
	assert_tsk_context();
	assert(tsk);
	assert(tsk->hdr.obj.res!=RELEASED);  // object with released resources cannot be used
	assert(state);

	sys_lock();
	{
		if (tsk->hdr.id == ID_STOPPED && // task is already active
		    tsk->join != DETACHED)       // detached task cannot be (re)started
		{
			tsk->state = state;

			core_ctx_init(tsk);
			core_tsk_insert(tsk);
		}
	}
	sys_unlock();
}

/* -------------------------------------------------------------------------- */
unsigned tsk_detach( tsk_t *tsk )
/* -------------------------------------------------------------------------- */
{
	unsigned event;

	assert_tsk_context();
	assert(tsk);
	assert(tsk->hdr.obj.res!=RELEASED);

	sys_lock();
	{
		if (tsk->join == DETACHED ||   // task has already been detached
		    tsk->hdr.obj.res == 0)     // task is undetachable
			event = E_FAILURE;
		else
		if (tsk->hdr.id == ID_STOPPED) // task is already inactive
		{
			core_res_free(&tsk->hdr.obj.res);
			event = E_SUCCESS;
		}
		else                           // task is active and can be detached
		{
			core_tsk_wakeup(tsk->join, E_FAILURE);
			tsk->join = DETACHED;
			event = E_SUCCESS;
		}
	}
	sys_unlock();

	return event;
}

/* -------------------------------------------------------------------------- */
unsigned tsk_join( tsk_t *tsk )
/* -------------------------------------------------------------------------- */
{
	unsigned event;

	assert_tsk_context();
	assert(tsk);
	assert(tsk->hdr.obj.res!=RELEASED);

	sys_lock();
	{
		if (tsk->join != JOINABLE ||   // task is unjoinable
		    tsk == System.cur)         // deadlock detected
			event = E_FAILURE;
		else
		if (tsk->hdr.id == ID_STOPPED) // task is already inactive
			event = E_SUCCESS;
		else                           // task is active; wait for termination
			event = core_tsk_waitFor(&tsk->join, INFINITE);

		if (event != E_FAILURE &&      // task has not been detached
		    tsk->hdr.id == ID_STOPPED) // task is still inactive
			core_res_free(&tsk->hdr.obj.res);
	}
	sys_unlock();

	return event;
}

/* -------------------------------------------------------------------------- */
static
void priv_tsk_terminator( void )
/* -------------------------------------------------------------------------- */
{
	tsk_t *tsk;

	sys_lock();
	{
		while (IDLE.hdr.obj.queue)
		{
			tsk = IDLE.hdr.obj.queue;
			IDLE.hdr.obj.queue = tsk->hdr.obj.queue;
			core_res_free(&tsk->hdr.obj.res);
		}

		IDLE.state = core_tsk_idle;
	}
	sys_unlock();
}

/* -------------------------------------------------------------------------- */
static
void priv_tsk_destroy( tsk_t *tsk )
/* -------------------------------------------------------------------------- */
{
	tsk->hdr.obj.queue = IDLE.hdr.obj.queue;
	IDLE.hdr.obj.queue = tsk;
	IDLE.state = priv_tsk_terminator;
}

/* -------------------------------------------------------------------------- */
void tsk_stop( void )
/* -------------------------------------------------------------------------- */
{
	assert_tsk_context();
	assert(System.cur->mtx.list == 0);

	port_set_lock();

	if (System.cur->join != DETACHED)
		core_tsk_wakeup(System.cur->join, E_SUCCESS);
	else
		priv_tsk_destroy(System.cur); // current task will be destroyed later

	core_tsk_remove(System.cur);

	assert(!"system can not return here");
	for (;;);                         // disable unnecessary warning
}

/* -------------------------------------------------------------------------- */
void tsk_kill( tsk_t *tsk )
/* -------------------------------------------------------------------------- */
{
	mtx_t *mtx;
	mtx_t *nxt;

	assert_tsk_context();
	assert(tsk);
	assert(tsk->hdr.obj.res!=RELEASED);

	sys_lock();
	{
		if (tsk->hdr.id != ID_STOPPED && // inactive task cannot be reseted
		    tsk->join != DETACHED)       // detached task cannot be reseted
		{
			tsk->mtx.tree = 0;
			for (mtx = tsk->mtx.list; mtx; mtx = nxt)
			{
				nxt = mtx->list;
				if ((mtx->mode & mtxRobust))
					if (core_mtx_transferLock(mtx, OWNERDEAD) == 0)
						mtx->mode |= mtxInconsistent;
			}

			core_tsk_wakeup(tsk->join, E_STOPPED);

			if (tsk->hdr.id == ID_READY)
				core_tsk_remove(tsk);
			else
			if (tsk->hdr.id == ID_BLOCKED)
			{
				core_tsk_unlink((tsk_t *)tsk, E_STOPPED);
				core_tmr_remove((tmr_t *)tsk);
			}
		}
	}
	sys_unlock();
}

/* -------------------------------------------------------------------------- */
void tsk_delete( tsk_t *tsk )
/* -------------------------------------------------------------------------- */
{
	assert_tsk_context();
	assert(tsk);
	assert(tsk->hdr.obj.res!=RELEASED);

	tsk_kill(tsk);
	tsk_detach(tsk);
}

/* -------------------------------------------------------------------------- */
void tsk_yield( void )
/* -------------------------------------------------------------------------- */
{
	assert_tsk_context();

	sys_lock();
	{
		core_ctx_switchNow();
	}
	sys_unlock();
}

/* -------------------------------------------------------------------------- */
void tsk_flip( fun_t *state )
/* -------------------------------------------------------------------------- */
{
	assert_tsk_context();
	assert(state);

	port_set_lock();

	System.cur->state = state;

	core_ctx_switch();
	core_tsk_flip((void *)STK_CROP(System.cur->stack, System.cur->size));
}

/* -------------------------------------------------------------------------- */
void tsk_setPrio( unsigned prio )
/* -------------------------------------------------------------------------- */
{
	assert_tsk_context();

	sys_lock();
	{
		System.cur->basic = prio;
		core_cur_prio(prio);
	}
	sys_unlock();
}

/* -------------------------------------------------------------------------- */
unsigned tsk_getPrio( void )
/* -------------------------------------------------------------------------- */
{
	unsigned prio;

	assert_tsk_context();

	sys_lock();
	{
		prio = System.cur->basic;
	}
	sys_unlock();

	return prio;
}

/* -------------------------------------------------------------------------- */
unsigned tsk_waitFor( unsigned flags, cnt_t delay )
/* -------------------------------------------------------------------------- */
{
	unsigned event;

	assert_tsk_context();

	sys_lock();
	{
		System.cur->tmp.flg.flags = flags;
		event = core_tsk_waitFor(&System.wai, delay);
	}
	sys_unlock();

	return event;
}

/* -------------------------------------------------------------------------- */
unsigned tsk_waitUntil( unsigned flags, cnt_t time )
/* -------------------------------------------------------------------------- */
{
	unsigned event;

	assert_tsk_context();

	sys_lock();
	{
		System.cur->tmp.flg.flags = flags;
		event = core_tsk_waitUntil(&System.wai, time);
	}
	sys_unlock();

	return event;
}

/* -------------------------------------------------------------------------- */
unsigned tsk_give( tsk_t *tsk, unsigned flags )
/* -------------------------------------------------------------------------- */
{
	unsigned event;

	assert(tsk);
	assert(tsk->hdr.obj.res!=RELEASED);

	sys_lock();
	{
		if (tsk->guard == &System.wai)
		{
			if (tsk->tmp.flg.flags & flags)
				flags = tsk->tmp.flg.flags &= ~flags;

			if (tsk->tmp.flg.flags == 0)
				core_tsk_wakeup(tsk, flags);

			event = E_SUCCESS;
		}
		else
			event = E_FAILURE;
	}
	sys_unlock();

	return event;
}

/* -------------------------------------------------------------------------- */
void tsk_sleepFor( cnt_t delay )
/* -------------------------------------------------------------------------- */
{
	sys_lock();
	{
		core_tsk_waitFor(&System.dly, delay);
	}
	sys_unlock();
}

/* -------------------------------------------------------------------------- */
void tsk_sleepNext( cnt_t delay )
/* -------------------------------------------------------------------------- */
{
	sys_lock();
	{
		core_tsk_waitNext(&System.dly, delay);
	}
	sys_unlock();
}

/* -------------------------------------------------------------------------- */
void tsk_sleepUntil( cnt_t time )
/* -------------------------------------------------------------------------- */
{
	sys_lock();
	{
		core_tsk_waitUntil(&System.dly, time);
	}
	sys_unlock();
}

/* -------------------------------------------------------------------------- */
unsigned tsk_suspend( tsk_t *tsk )
/* -------------------------------------------------------------------------- */
{
	unsigned event;

	assert(tsk);
	assert(tsk->hdr.obj.res!=RELEASED);

	sys_lock();
	{
		if (tsk->hdr.id == ID_READY)
		{
			core_tsk_suspend(tsk);
			event = E_SUCCESS;
		}
		else
			event = E_FAILURE;
	}
	sys_unlock();

	return event;
}

/* -------------------------------------------------------------------------- */
unsigned tsk_resume( tsk_t *tsk )
/* -------------------------------------------------------------------------- */
{
	unsigned event;

	assert(tsk);
	assert(tsk->hdr.obj.res!=RELEASED);

	sys_lock();
	{
		if (tsk->guard == &System.dly && tsk->delay == INFINITE)
		{
			core_tsk_wakeup(tsk, 0); // unused event value
			event = E_SUCCESS;
		}
		else
			event = E_FAILURE;
	}
	sys_unlock();

	return event;
}

/* -------------------------------------------------------------------------- */
