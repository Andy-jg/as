/**
 * AS - the open source Automotive Software on https://github.com/parai
 *
 * Copyright (C) 2018  AS <parai@foxmail.com>
 *
 * This source code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation; See <http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */
/* ============================ [ INCLUDES  ] ====================================================== */
#include "kernel_internal.h"
#include "asdebug.h"
#ifdef USE_SMP
#include "spinlock.h"
#endif
/* ============================ [ MACROS    ] ====================================================== */
#define AS_LOG_OS  0
#define AS_LOG_OSE 1
#define AS_LOG_SMP 1
/* ============================ [ TYPES     ] ====================================================== */
/* ============================ [ DECLARES  ] ====================================================== */
extern void Os_PortResume(void);
extern void Os_PortActivate(void);
extern void Os_PortStartSysTick(void);
#ifdef USE_SMP
extern void secondary_start(void);
extern void Ipc_KickTo(int cpu, int irqno);
extern void Irq_Install(int irqno, void (*handler)(void), int oncpu);
#endif
/* ============================ [ DATAS     ] ====================================================== */
#ifdef USE_SMP
static spinlock_t knlSpinlock;
uint32 ISR2Counter[CPU_CORE_NUMBER];
#else
uint32 ISR2Counter;
#endif
/* ============================ [ LOCALS    ] ====================================================== */
#ifdef USE_SMP
static void Os_PortSchedule(void)
{
	DECLARE_SMP_PROCESSOR_ID();
	ASLOG(SMP, "Os_PortSchedule on CPU%d!\n", cpuid);
}
#endif
/* ============================ [ FUNCTIONS ] ====================================================== */
void Os_PortActivateImpl(void)
{
	DECLARE_SMP_PROCESSOR_ID();

	/* get internal resource or NON schedule */
	RunningVar->priority = RunningVar->pConst->runPriority;

#ifdef USE_SMP
	ASLOG(OS, "%s(%d) is running on CPU %d\n", RunningVar->pConst->name,
			RunningVar->pConst->initPriority, cpuid);
#else
	ASLOG(OS, "%s(%d) is running\n", RunningVar->pConst->name,
			RunningVar->pConst->initPriority);
#endif

	OSPreTaskHook();

	CallLevel = TCL_TASK;
	Irq_Enable();

	RunningVar->pConst->entry();

	/* Should not return here */
	TerminateTask();
}

void Os_PortInit(void)
{
#ifdef USE_SMP
	memset(ISR2Counter, 0, sizeof(ISR2Counter));
	Irq_Install(0, Os_PortSchedule, 0);
	Irq_Install(1, Os_PortSchedule, 1);
#else
	ISR2Counter = 0;
	Os_PortStartSysTick();
#endif
}

void Os_PortInitContext(TaskVarType* pTaskVar)
{
	/* 8 byte aligned */
	pTaskVar->context.sp = (void*)((uint64_t)(pTaskVar->pConst->pStack + pTaskVar->pConst->stackSize - 8)&(~(uint64_t)0x7UL));
	pTaskVar->context.pc = Os_PortActivate;
}


void Os_PortDispatch(void)
{
	__asm("svc 0");
}

void Os_PortStartDispatch(void)
{
	DECLARE_SMP_PROCESSOR_ID();

	RunningVar = NULL;
	Os_PortDispatch();
	asAssert(0);
}
void Os_PortIdle(void)
{
	DECLARE_SMP_PROCESSOR_ID();
	#ifdef USE_SMP
	ASLOG(OSE, "!!!CPU%d enter PortIdle!!!\n", SMP_PROCESSOR_ID());
	#else
	ASLOG(OSE, "!!!enter PortIdle!!!\n");
	#endif

	asAssert(0);
}
#ifdef USE_SMP
void Os_PortSpinLock(void)
{
	spin_lock(&knlSpinlock);
}

void Os_PortSpinUnLock(void)
{
	spin_unlock(&knlSpinlock);
}

TASK(TaskIdle2)
{
	DECLARE_SMP_PROCESSOR_ID();

	RunningVar->priority = 0;

	ASLOG(SMP, "TaskIdle2 is running on CPU%d\n", smp_processor_id());

	for(;;)
	{
		Schedule();
	}
}

void secondary_main(void)
{
	DECLARE_SMP_PROCESSOR_ID();

	ASLOG(SMP, "!!!CPU%d is up!!!\n", SMP_PROCESSOR_ID());

	Os_PortSpinLock();
	Sched_GetReady();
	Os_PortStartDispatch();
	while(1);
}

void Os_PortRequestSchedule(uint8 cpu)
{
	Ipc_KickTo((int)cpu, (int)cpu);
}

void Os_PortStartFirstDispatch(void)
{
	ASLOG(SMP, "!!!CPU%d is up!!!\n", smp_processor_id());
	smp_boot_secondary(1, secondary_start);
	Os_PortStartSysTick();

	Os_PortStartDispatch();
}
#endif

void Os_PortException(long exception, void* sp, long esr)
{
	ASLOG(OSE, "Exception %d happened!\n", exception);
	asAssert(0);
}

void EnterISR(void)
{
	/* do nothing */
}

void LeaveISR(void)
{
	/* do nothing */
}

#ifdef USE_PTHREAD_SIGNAL
void Os_PortCallSignal(int sig, void (*handler)(int), void* sp, void (*pc)(void))
{
	DECLARE_SMP_PROCESSOR_ID();

	asAssert(NULL != handler);

	handler(sig);

	/* restore its previous stack */
	RunningVar->context.sp = sp;
	RunningVar->context.pc = pc;
}

void Os_PortExitSignalCall(void)
{
	Sched_GetReady();
	Os_PortStartDispatch();
}

int Os_PortInstallSignal(TaskVarType* pTaskVar, int sig, void* handler)
{
	void* sp;
	uint64_t* stk;

	sp = pTaskVar->context.sp;

	if((sp - pTaskVar->pConst->pStack) < (pTaskVar->pConst->stackSize*3/4))
	{
		/* stack 75% usage, ignore this signal call */
		ASLOG(OS,"install signal %d failed\n", sig);
		return -1;
	}

	stk = sp;

	*(--stk) = (uint64_t)handler;                /* x1 */
	*(--stk) = (uint64_t)sig;                    /* x0 */
	*(--stk) = (uint64_t)pTaskVar->context.pc;   /* x3 */
	*(--stk) = (uint64_t)sp;                     /* x2 */

	*(--stk) = 5 ;                               /* x5  */
	*(--stk) = 4 ;                               /* x4  */
	*(--stk) = 7 ;                               /* x7  */
	*(--stk) = 6 ;                               /* x6  */
	*(--stk) = 9 ;                               /* x9  */
	*(--stk) = 8 ;                               /* x8  */
	*(--stk) = 11;                               /* x11 */
	*(--stk) = 10;                               /* x10 */
	*(--stk) = 13;                               /* x13 */
	*(--stk) = 12;                               /* x12 */
	*(--stk) = 15;                               /* x15 */
	*(--stk) = 14;                               /* x14 */
	*(--stk) = 17;                               /* x17 */
	*(--stk) = 16;                               /* x16 */
	*(--stk) = 19;                               /* x19 */
	*(--stk) = 18;                               /* x18 */
	*(--stk) = 21;                               /* x21 */
	*(--stk) = 20;                               /* x20 */
	*(--stk) = 23;                               /* x23 */
	*(--stk) = 22;                               /* x22 */
	*(--stk) = 25;                               /* x25 */
	*(--stk) = 24;                               /* x24 */
	*(--stk) = 27;                               /* x27 */
	*(--stk) = 26;                               /* x26 */
	*(--stk) = 29;                               /* x29 */
	*(--stk) = 28;                               /* x28 */

	*(--stk) = (uint64_t)Os_PortExitSignalCall;  /* x30 */
	*(--stk) = (uint64_t)Os_PortCallSignal;      /* elr_el1 */
	*(--stk) = 0x20000305;                       /* spsr_el1 */

	pTaskVar->context.sp = stk;
	pTaskVar->context.pc = Os_PortResume;

	return 0;
}
#endif
