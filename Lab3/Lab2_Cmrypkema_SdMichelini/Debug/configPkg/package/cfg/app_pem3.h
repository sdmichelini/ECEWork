/*
 *  Do not modify this file; it is automatically 
 *  generated and any modifications will be overwritten.
 *
 * @(#) xdc-z44
 */

#include <xdc/std.h>

#include <ti/sysbios/family/arm/m3/Hwi.h>
extern const ti_sysbios_family_arm_m3_Hwi_Handle ti_sysbios_family_arm_m3_Hwi0;

#include <ti/sysbios/knl/Clock.h>
extern const ti_sysbios_knl_Clock_Handle clock0;

#include <ti/sysbios/knl/Semaphore.h>
extern const ti_sysbios_knl_Semaphore_Handle buttonScanSem;

#include <ti/sysbios/knl/Mailbox.h>
extern const ti_sysbios_knl_Mailbox_Handle buttonMailbox;

#include <ti/sysbios/knl/Semaphore.h>
extern const ti_sysbios_knl_Semaphore_Handle displaySem;

#include <ti/sysbios/knl/Semaphore.h>
extern const ti_sysbios_knl_Semaphore_Handle waveformSem;

#include <ti/sysbios/knl/Semaphore.h>
extern const ti_sysbios_knl_Semaphore_Handle settingsSem;

#include <ti/sysbios/knl/Semaphore.h>
extern const ti_sysbios_knl_Semaphore_Handle localBufSem;

#include <ti/sysbios/knl/Task.h>
extern const ti_sysbios_knl_Task_Handle buTask;

#include <ti/sysbios/knl/Task.h>
extern const ti_sysbios_knl_Task_Handle waveTask;

#include <ti/sysbios/knl/Task.h>
extern const ti_sysbios_knl_Task_Handle disTask;

#include <ti/sysbios/knl/Task.h>
extern const ti_sysbios_knl_Task_Handle uiTask;

#include <ti/sysbios/knl/Semaphore.h>
extern const ti_sysbios_knl_Semaphore_Handle fftSem;

#include <ti/sysbios/knl/Task.h>
extern const ti_sysbios_knl_Task_Handle fTask;

#include <ti/sysbios/family/arm/m3/Hwi.h>
extern const ti_sysbios_family_arm_m3_Hwi_Handle ti_sysbios_family_arm_m3_Hwi1;

extern int xdc_runtime_Startup__EXECFXN__C;

extern int xdc_runtime_Startup__RESETFXN__C;

#ifndef ti_sysbios_knl_Task__include
#ifndef __nested__
#define __nested__
#include <ti/sysbios/knl/Task.h>
#undef __nested__
#else
#include <ti/sysbios/knl/Task.h>
#endif
#endif

extern ti_sysbios_knl_Task_Struct TSK_idle;

