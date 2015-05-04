/*
 *  ======== main.c ========
 */

#include <xdc/std.h>
#include <xdc/cfg/global.h>
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
#include "driverlib/timer.h"

#include "utils/ustdlib.h"


/*
 *  ======== main ========
 */
void configureComparator(void);
void configureTimerACapture(void);
void CaptureIsr(void);
void canTask(UArg a0, UArg a1);

#define SAMPLE_SIZE 10

//Init Samples
//Clears the Sample Buffer
void InitSamples();
//Moving-Average Filter
void AddDataSample(long sample);
//Get's the average
unsigned long GetAverageSamples();

//Filter Variables
int g_filterIndex;
int g_size;
volatile unsigned long g_sum;
//Circular Buffer to Hold the Samples
long g_samples[SAMPLE_SIZE];
long g_samplesCollected;

void periodicScan(UArg arg0);

Void main()
{ 

	InitSamples();
	g_sum = 0;
	g_samplesCollected = 0;
	configureComparator();
	configureTimerACapture();
	NetworkInit();
	BIOS_start();     /* enable interrupts and start SYS/BIOS */
}

void canTask(UArg a0, UArg a1){
	while(1){
		//Pend on a semaphore
		Semaphore_pend(averageSem, BIOS_WAIT_FOREVER);

		//Get the average period
		//Convert to mHz in form of unsigned long(Create a function to do this)
		//Send over CAN

		long average = GetAverageSamples();

		//average is period in clk cycles
		float period = (float)average/250000000.0f;
		float frequency = 1.0f/period;
		unsigned long mHz = (unsigned long)(frequency * 1000.0f);

		NetworkTx(mHz);
	}
}

void periodicScan(UArg ar0){
	Semaphore_post(averageSem);
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
		lastVal = curVal;
		g_sum += diff;
		g_samplesCollected++;
	}
}

void InitSamples(){
	unsigned int i; 
	//Set the buffer to zero initially
	for(i = 0; i < SAMPLE_SIZE; i++){
		g_samples[i] = 0;
	}
	g_filterIndex = 0;
	//Accounts for First couple samples
	g_size = 0;
}
/*
 * Add's a Data Sample to the Circular Buffer
 */
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
/*
 * Get's the Average from the Samples Buffer
 */
unsigned long GetAverageSamples(){
	if(g_samplesCollected == 0){//Don't divide by zero
		return 0;
	}else{//Average = Sum/Collected
		IArg key;
		key = GateHwi_enter(gateHwi0);
		unsigned long temp = g_sum/g_samplesCollected;
		g_sum = 0;
		g_samplesCollected = 0;
		GateHwi_leave(gateHwi0, key);
		return temp;
	}
}

void configureTimerACapture(void){
	SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
	GPIODirModeSet(GPIO_PORTB_BASE, GPIO_PIN_0, GPIO_DIR_MODE_HW); // CCP0 input
	GPIOPadConfigSet(GPIO_PORTB_BASE, GPIO_PIN_0, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
	TimerDisable(TIMER0_BASE, TIMER_BOTH);
	TimerConfigure(TIMER0_BASE, TIMER_CFG_SPLIT_PAIR | TIMER_CFG_A_CAP_TIME);
	TimerControlEvent(TIMER0_BASE, TIMER_A, TIMER_EVENT_POS_EDGE);
	TimerLoadSet(TIMER0_BASE, TIMER_A, 0xffff);
	TimerIntEnable(TIMER0_BASE, TIMER_CAPA_EVENT);
	TimerEnable(TIMER0_BASE, TIMER_A);
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

