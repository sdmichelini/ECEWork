/*
 *  ======== main.c ========
 */

#include <xdc/std.h>

#include <xdc/runtime/Error.h>
#include <xdc/runtime/System.h>

#include <ti/sysbios/BIOS.h>

#include <ti/sysbios/knl/Task.h>

#include "lm3s2110.h"
#include "network.h"

#include "inc/hw_comp.h"
#include "inc/hw_types.h"
#include "inc/hw_can.h"
#include "inc/hw_memmap.h"
#include "inc/hw_gpio.h"

#include "driverlib/comp.h"
#include "driverlib/can.h"
#include "driverlib/sysctl.h"
#include "driverlib/gpio.h"

///*
// *  ======== taskFxn ========
// */
//Void taskFxn(UArg a0, UArg a1)
//{
//    System_printf("enter taskFxn()\n");
//
//    Task_sleep(10);
//
//    System_printf("exit taskFxn()\n");
//}

/*
 *  ======== main ========
 */
void configureComparator(void);
void CaptureIsr(void);

#define SAMPLE_SIZE 10

//Init Samples
//Clears the Sample Buffer
void InitSamples();
//Moving-Average Filter
void AddDataSample(long sample);
//Get's the average
long GetAverageSamples();

//Filter Variables
int g_filterIndex;
int g_size;
//Buffer to Hold the Samples
long g_samples[SAMPLE_SIZE];

Void main()
{ 
//    Task_Handle task;
//    Error_Block eb;
//
//    System_printf("enter main()\n");
//
//    Error_init(&eb);
//    task = Task_create(taskFxn, NULL, &eb);
//    if (task == NULL) {
//        System_printf("Task_create() failed!\n");
//        BIOS_exit(0);
//    }
	InitSamples();
	configureComparator();
    BIOS_start();     /* enable interrupts and start SYS/BIOS */
}

/*
HWI for capturing period
*/
void CaptureIsr(void){
	//Clear Interrupt Flag
	TIMER0_ICR_R = TIMER_ICR_CAECINT;

	static long lastVal = 0;

	if(lastVal == 0){
		lastVal = TIMER0_TAR_R;//Grab the time if we don't have one
	}else{
		long curVal = TIMER0_TAR_R;

		//Catch the rool-over cases by anding w/ 0xffff
		long diff = (curVal - lastVal)&0xffff;

		//diff is the period in clk ticks

		//Now we need to find a way to average diff
		//Circular Buffer and adding diff/sizeof(buf) each time???
		//Is it shared-data safe
		//Do we care if samples in array get modified
	}
}

void InitSamples(){
	unsigned int i; 
	for(i = 0; i < SAMPLE_SIZE; i++){
		g_samples[i] = 0;
	}
	g_filterIndex = 0;
	g_empty = 0;
}

void AddDataSample(long sample){
	if(g_filterIndex > SAMPLE_SIZE){
		g_filterIndex = 0;
	}	

	g_samples[g_filterIndex] = sample;
	if(g_size < SAMPLE_SIZE){
		g_size++;
	}
	g_filterIndex++;
}

long GetAverageSamples(){
	unsigned int i;
	long sum = 0;
	for(i = 0; i < g_size; i++){
		sum+= g_samples[i];
	}
	if(g_size == 0){
		return 0;
	}else{
		return sum/g_size;
	}
}

void configureComparator(void){
	//ComparatorConfigure() settings, comparator reference voltage and the pin numbers for comparator I/O pins in Port B and Port D.
	SysCtlPeripheralEnable(SYSCTL_PERIPH_COMP0);
	ComparatorConfigure(COMP_BASE, 0, COMP_TRIG_NONE | COMP_INT_FALL | COMP_ASRCP_REF | COMP_OUTPUT_NORMAL);
	ComparatorRefSet(COMP_BASE, COMP_REF_1_5125V);
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
	GPIODirModeSet(GPIO_PORTB_BASE, GPIO_PIN_4, GPIO_DIR_MODE_HW); // C0- input
	GPIOPadConfigSet(GPIO_PORTB_BASE, GPIO_PIN_4, GPIO_STRENGTH_2MA,GPIO_PIN_TYPE_ANALOG);
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
	GPIODirModeSet(GPIO_PORTD_BASE, GPIO_PIN_7, GPIO_DIR_MODE_HW); // C0o output
	GPIOPadConfigSet(GPIO_PORTD_BASE, GPIO_PIN_7, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);

}

