/*
 *  ======== main.c ========
 */

#include <xdc/std.h>
#include <xdc/cfg/global.h>
#include <xdc/runtime/Error.h>
#include <xdc/runtime/System.h>

#include <ti/sysbios/BIOS.h>

#include <ti/sysbios/knl/Task.h>
#include "inc/hw_types.h"
#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"
#include "inc/hw_sysctl.h"
#include "inc/lm3s8962.h"
#include "driverlib/sysctl.h"
#include "driverlib/adc.h"

#include "frame_graphics.h"
#include "utils/ustdlib.h"

#include "drivers/rit128x96x4.h"

#include "driverlib/timer.h"
#include "driverlib/interrupt.h"

#include "driverlib/gpio.h"
#include "buttons.h"

#include <math.h>

//ADC Variables
#define ADC_BUFFER_SIZE 2048//size of the buffer(power of 2)
#define ADC_BUFFER_WRAP(i) ((i) & (ADC_BUFFER_SIZE - 1))//index wrapping macro
volatile int g_iADCBufferIndex = ADC_BUFFER_SIZE - 1; //latest sample index
volatile unsigned short g_pusADCBuffer[ADC_BUFFER_SIZE]; //circular buffer of ADC samples
volatile unsigned long g_ulADCErrors = 0; //ADC missed deadlines
//ADC Bits
#define ADCBITCNT 10 //10 bit ADC
#define PIXELCNT 12
#define VIN 6 //In V

//Button Press Variables
#define SELECT_BUTTON 0x1
#define UP_BUTTON (0x1 << 1)
#define DOWN_BUTTON (0x1 << 2)
#define LEFT_BUTTON (0x1 << 3)
#define RIGHT_BUTTON (0x1 << 4)

//Menu State
#define EDIT_TIMESCALE 0
#define EDIT_VOLTAGE 1
#define EDIT_OPTIONS 3//# of selections in each submenu

//Drawing Constants
//Horizontal and Vertical Lines
#define HORIZONTAL_DIVS 8
#define VERTICAL_DIVS 11

//Trigger State
//What type of trigger it is
typedef enum {
	kRisingEdge, kFallingEdge
} TriggerState;

//User Interface Variables
TriggerState g_triggerState;
//Menu Selection that is Currently Selected for Editing
short g_editing;
//What voltage div are we on
short g_voltageDiv;
//What scale are we on
short g_scaleDiv;

//Voltage Scales
const char * const g_ppcVoltageScaleStr[] = {
		"100 mV","200 mV","500 mV","1 V"
};

//Voltage Settings
float g_voltageDivArray[] = {
		0.1f, 0.2f, 0.5f, 1.0f
};

//Timer values
//In us
unsigned long g_timerValues[] = {
		24, 48, 72, 96
};

//Timer Scales
const char * const g_ppcTimeScaleStr[] = {
		"24 us","48 us","72 us","96 us"
};


/*
 *  ======== taskFxn ========
 */
void configureAdc(void);
void ADCISR(void);
//Button Tick Callback
void buttonClock(UArg arg0);

void buttonTask(UArg arg0, UArg arg1);

//User Input Task
void userInputTask(UArg arg0, UArg arg1);

//Display Task
void displayTask(UArg arg0, UArg arg1);

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
 Void main() {
	 //Task_Handle task;
	 Error_Block eb;

	 System_printf("enter main()\n");

	 Error_init(&eb);
	 //    task = Task_create(taskFxn, NULL, &eb);
	 //    if (task == NULL) {
	 //        System_printf("Task_create() failed!\n");
	 //        BIOS_exit(0);
	 //    }

	 BIOS_start(); /* enable interrupts and start SYS/BIOS */
 }

 void ADCISR(void) {
	 ADC0_ISC_R = ADC_ISC_IN0; // clear ADC sequence0 interrupt flag in the ADCISC
	 //ADC overflow checking
	 if (ADC0_OSTAT_R & ADC_OSTAT_OV0) {
		 g_ulADCErrors++;
		 ADC0_OSTAT_R = ADC_OSTAT_OV0;
	 }

	 int buffer_index = ADC_BUFFER_WRAP(g_iADCBufferIndex + 1);
	 //Read from ADC
	 g_pusADCBuffer[buffer_index] = ADC0_SSFIFO0_R;
	 g_iADCBufferIndex = buffer_index;
 }

 void configureAdc(void) {
	 unsigned int i;
	 //Clear ADC Buffer
	 for (i = 0; i < ADC_BUFFER_SIZE; i++) {
		 g_pusADCBuffer[i] = 0;
	 }

	 //enable adc
	 SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);
	 //Set to 500ksps
	 SysCtlADCSpeedSet(SYSCTL_ADCSPEED_500KSPS);
	 //Disable the ADC before continuing
	 //Sequence 0
	 ADCSequenceDisable(ADC0_BASE, 0);
	 //"always trigger"
	 //Highest Priority
	 ADCSequenceConfigure(ADC0_BASE, 0, ADC_TRIGGER_ALWAYS, 0);
	 //Configure the Sequence Step
	 ADCSequenceStepConfigure(ADC0_BASE, 0, 0,
			 ADC_CTL_IE | ADC_CTL_END | ADC_CTL_CH0);
	 //Enable ADC Interrupt from Sequence 0
	 ADCIntEnable(ADC0_BASE, 0);
	 //Enabled ADC interrupt from sequence 0
	 ADCSequenceEnable(ADC0_BASE, 0);
	 //Enable the Interrupts
	 //IntPrioritySet(INT_ADC0SS0, 0x00);
	 //IntEnable(INT_ADC0SS0);

 }

 void buttonClock(UArg arg0) {
	 //Notify the buttonTask that we need to scan the inputs
	 Semaphore_post(buttonScanSem);
 }
 /*
  * Essentially this task waits to be notified from the buttonClock()
  * Then it processes the button input and posts any input to a mailbox
  */
 void buttonTask(UArg arg0, UArg arg1) {
	 unsigned long presses;
	 while (1) {
		 Semaphore_pend(buttonScanSem, BIOS_WAIT_FOREVER);

		 presses = g_ulButtons;
		 //Debounce the Buttons
		 ButtonDebounce(
				 ((~GPIO_PORTF_DATA_R & GPIO_PIN_1) >> 1)
				 | ((~GPIO_PORTE_DATA_R
						 & (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2
								 | GPIO_PIN_3)) << 1));
		 //get the presses
		 presses = ~presses & g_ulButtons; // button presses

		 char msg;
		 //Put the button that was pressed
		 if (presses & SELECT_BUTTON) {
			 msg = SELECT_BUTTON;
			 Mailbox_post(buttonMailbox, &msg, BIOS_WAIT_FOREVER);
		 }
		 if (presses & UP_BUTTON) {
			 msg = UP_BUTTON;
			 Mailbox_post(buttonMailbox, &msg, BIOS_WAIT_FOREVER);
		 }
		 if (presses & DOWN_BUTTON) {
			 msg = DOWN_BUTTON;
			 Mailbox_post(buttonMailbox, &msg, BIOS_WAIT_FOREVER);
		 }
		 if (presses & LEFT_BUTTON) {
			 msg = LEFT_BUTTON;
			 Mailbox_post(buttonMailbox, &msg, BIOS_WAIT_FOREVER);
		 }
		 if (presses & RIGHT_BUTTON) {
			 msg = RIGHT_BUTTON;
			 Mailbox_post(buttonMailbox, &msg, BIOS_WAIT_FOREVER);
		 }

	 }
 }

 void userInputTask(UArg arg0, UArg arg1) {
	 char msg;
	 while (1) {
		 Mailbox_pend(buttonMailbox, &msg, BIOS_WAIT_FOREVER);
		 switch(msg){

		 case SELECT_BUTTON:
			 //Select button pushed
		 {
			 if (g_triggerState == kRisingEdge) { //Switch Trigger Edge
				 g_triggerState = kFallingEdge;
			 } else {
				 g_triggerState = kRisingEdge;
			 }
			 break;
		 }
		 case UP_BUTTON:
			 //Up Button Pressed
		 {
			 if (g_editing == EDIT_VOLTAGE) {
				 if (g_voltageDiv < EDIT_OPTIONS) { //Increment Voltage Scale Unless it Reaches it's Max
					 g_voltageDiv++;
					 g_scaleDiv = (VIN * PIXELCNT)
											/ ((1 << ADCBITCNT) * g_voltageDivArray[g_voltageDiv]); //Readjust the Scale
				 }
			 } else if (g_editing == EDIT_TIMESCALE) {
				 //				 if (timerDiv < EDIT_OPTIONS) { //Increment Time Scale Unless it Reaches it's Max
				 //					 timerDiv++;
				 //					 setupAdcTimer(g_timerValues[timerDiv]); //Reconfigure ADC Trigger Timer
				 //					 configureAdc_timer(); //Reconfigure ADC
				 //				 }
			 }
			 break;
		 }
		 case DOWN_BUTTON:
			 //Down Button Pressed
		 {
			 if (g_editing == EDIT_VOLTAGE) {
				 if (g_voltageDiv > 0) { //Decrement to lowest menu setting
					 g_voltageDiv--;
					 g_scaleDiv = (VIN * PIXELCNT)
											/ ((1 << ADCBITCNT) * g_voltageDivArray[g_voltageDiv]); //Redo Scale
				 }
			 } else if (g_editing == EDIT_TIMESCALE) {
				 //				 if (timerDiv > 0) { //Decrement to lowest menu setting
				 //					 timerDiv--;
				 //					 //Redo ADC and Timer
				 //					 setupAdcTimer(g_timerValues[timerDiv]);
				 //					 configureAdc_timer();
				 //				 }
			 }
			 break;
		 }
		 case LEFT_BUTTON:
			 //Left Button Pressed
		 {
			 g_editing = EDIT_TIMESCALE; //Switch to Time
			 break;
		 }
		 case RIGHT_BUTTON:
			 //Right Button Pressed
		 {
			 g_editing = EDIT_VOLTAGE; //Switch to Voltage
			 break;
		 }
		 default:

			 break;
		 }
	 }

 }

 void displayTask(UArg arg0, UArg arg1){
	 while(1){

	 }
 }

