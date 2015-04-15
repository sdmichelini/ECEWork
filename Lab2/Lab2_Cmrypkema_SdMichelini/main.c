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

//ADC Offset
#define ADC_OFFSET 510

//Waveform Buffer
short g_localBuffer[FRAME_SIZE_X];

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

#define GRID_BRIGHTNESS 0x5
#define TEXT_BRIGHTNESS 0xf

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
void configureTimerA0();
void configureGpio();
void ADCISR(void);
//Button Tick Callback
void buttonClock(UArg arg0);

void buttonTask(UArg arg0, UArg arg1);

//User Input Task
void userInputTask(UArg arg0, UArg arg1);

//Display Task
void displayTask(UArg arg0, UArg arg1);

//Waveform Task
void waveformTask(UArg arg0, UArg arg1);

unsigned long g_ulSystemClock;

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

	// initialize the clock generator
	//	if (REVISION_IS_A2)
	//	{
	//		SysCtlLDOSet(SYSCTL_LDO_2_75V);
	//	}
	//
	//	SysCtlClockSet(SYSCTL_SYSDIV_4 | SYSCTL_USE_PLL | SYSCTL_OSC_MAIN |
	//			SYSCTL_XTAL_8MHZ);
	//	g_ulSystemClock = SysCtlClockGet();



	RIT128x96x4Init(3500000); // initialize the OLED display
	//Configure the ADC and GPIO
	configureAdc();
	configureGpio();

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

void configureGpio(){
	//Configure the GPIO push buttons
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
	GPIOPinTypeGPIOInput(GPIO_PORTF_BASE, GPIO_PIN_1);
	GPIOPadConfigSet(GPIO_PORTF_BASE, GPIO_PIN_1, GPIO_STRENGTH_2MA,
			GPIO_PIN_TYPE_STD_WPU);
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
	GPIOPinTypeGPIOInput(GPIO_PORTE_BASE, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 |
			GPIO_PIN_3);
	GPIOPadConfigSet(GPIO_PORTE_BASE, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 |
			GPIO_PIN_3, GPIO_STRENGTH_2MA,
			GPIO_PIN_TYPE_STD_WPU);
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

		//		presses = g_ulButtons;
		//		//Debounce the Buttons
		//		ButtonDebounce(
		//				((~GPIO_PORTF_DATA_R & GPIO_PIN_1) >> 1)
		//				| ((~GPIO_PORTE_DATA_R
		//						& (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2
		//								| GPIO_PIN_3)) << 1));
		//		//get the presses
		//		presses = ~presses & g_ulButtons; // button presses

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
		Semaphore_pend(settingsSem, BIOS_WAIT_FOREVER);
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
					g_scaleDiv = (VIN * PIXELCNT) / ((1 << ADCBITCNT) * g_voltageDivArray[g_voltageDiv]); //Readjust the Scale
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
		Semaphore_post(settingsSem);
		//Let the display know
		Semaphore_post(displaySem);
	}

}

void waveformTask(UArg arg0, UArg arg1){
	while(1){
		Semaphore_pend(waveformSem, BIOS_WAIT_FOREVER);
		//Where we start the search from(a half frame back)
		int startingIndex = ADC_BUFFER_WRAP(g_iADCBufferIndex - (FRAME_SIZE_Y / 2));
		//Where we first started
		int firstStart = startingIndex;
		//Whether or not we are finished
		short finished = 0;
		//How many loops we completed
		int it = 0;

		Semaphore_pend(settingsSem,BIOS_WAIT_FOREVER);
		TriggerState t = g_triggerState;
		Semaphore_post(settingsSem);
		//Go til we are done
		while(!finished){
			//The index before the one we search
			int prevIndex = ADC_BUFFER_WRAP(startingIndex - 1);




			//What trigger is it
			if(t == kRisingEdge){//Rising Edge
				if((g_pusADCBuffer[prevIndex] < ADC_OFFSET)&&(g_pusADCBuffer[startingIndex] >= ADC_OFFSET)){
					finished = 1;
					break;
				}
			}else{//Falling Edge
				if((g_pusADCBuffer[prevIndex] >= ADC_OFFSET)&&(g_pusADCBuffer[startingIndex] < ADC_OFFSET)){
					finished = 1;
					break;
				}
			}
			//Searched over half of the buffer
			if(it > (ADC_BUFFER_SIZE / 2)){//Can't Find Trigger
				finished = 1;
				startingIndex = firstStart;
				break;
			}
			//Increment
			it++;

			//Decrement the index
			startingIndex = prevIndex;
		}
		//Now copy to local buffer
		unsigned int i;

		//use a for loop
		Semaphore_pend(localBufSem,BIOS_WAIT_FOREVER);
		for(i = 0; i < FRAME_SIZE_X; i++){
			g_localBuffer[i] = g_pusADCBuffer[ADC_BUFFER_WRAP(startingIndex - (FRAME_SIZE_X / 2) + i)];
		}
		Semaphore_post(localBufSem);
		Semaphore_post(displaySem);
	}
}

void displayTask(UArg arg0, UArg arg1){
	while(1){
		Semaphore_pend(displaySem, BIOS_WAIT_FOREVER);

		//Now we can draw to the screen
		//First draw the background
		Semaphore_pend(settingsSem,BIOS_WAIT_FOREVER);
		short voltageDiv = g_voltageDiv;

		float scale_div = (VIN * PIXELCNT)/((1 << ADCBITCNT) * g_voltageDivArray[voltageDiv]);
		const char * timeScale = g_ppcTimeScaleStr[0];
		short editing = g_editing;

		TriggerState t = g_triggerState;
		Semaphore_post(settingsSem);

		FillFrame(0); // clear frame buffer

		//Now draw the grid
		unsigned int j;
		for(j = 0; j < VERTICAL_DIVS; j++){
			DrawLine((j * PIXELCNT) + 6, 0,(j * PIXELCNT) + 6, FRAME_SIZE_Y, GRID_BRIGHTNESS);//Draw Horizontal Lines
		}
		for(j = 0; j < HORIZONTAL_DIVS; j++){
			DrawLine(0, (j * PIXELCNT), FRAME_SIZE_X,(j * PIXELCNT), GRID_BRIGHTNESS);//Draw Vertical Lines
		}

		//Draw the selection
		//Draw either a box around time setting or voltage setting
		switch(editing){
		case EDIT_TIMESCALE:{
			DrawFilledRectangle(5, 3, 35, 10, 0x7);
			break;
		}
		case EDIT_VOLTAGE:{
			if(voltageDiv != 3){
				DrawFilledRectangle(44, 3, 79, 10, 0x7);
			}else{
				DrawFilledRectangle(44, 3, 44+20, 10, 0x7);
			}

			break;
		}
		default:
			break;
		}

		//Time Scale
		DrawString(6, 3, timeScale, 15, 0);

		//Voltage Div
		DrawString(44,3,g_ppcVoltageScaleStr[g_voltageDiv], 15, 0);

		//Trigger
		if(t == kRisingEdge){
			//Top line
			DrawLine(110, 0, 116, 0, 15);
			//Middle Line
			DrawLine(110, 0, 110, 8, 15);
			//Bottom Line
			DrawLine(104, 8, 110, 8, 15);
			//Triangle
			DrawLine(107, 6, 110, 3, 15);
			DrawLine(110, 3, 113, 6, 15);
		}else{
			//Bottom line
			DrawLine(110, 8, 116, 8, 15);
			//Middle Line
			DrawLine(110, 0, 110, 8, 15);
			//Top
			DrawLine(104, 0, 110, 0, 15);
			//Triangle
			DrawLine(107, 3, 110, 6, 15);
			DrawLine(110, 6, 113, 3, 15);
		}
		//Draw the points
		//Semaphore_pend(localBufSem, BIOS_WAIT_FOREVER);

		unsigned int i;
		for(i = 0; i < FRAME_SIZE_X - 1; i++){
			int y1 = FRAME_SIZE_Y/2 - (int)round((g_localBuffer[i] - ADC_OFFSET) * scale_div);
			int y2 = FRAME_SIZE_Y/2 - (int)round((g_localBuffer[i + 1] - ADC_OFFSET) * scale_div);
			DrawLine(i,y1,i+1,y2,0xf);
		}

		//Push the Drawing Buffer
		RIT128x96x4ImageDraw(g_pucFrame, 0, 0, FRAME_SIZE_X, FRAME_SIZE_Y);

		//Semaphore_post(localBufSem);
		Semaphore_post(waveformSem);

	}
}

