/*
 * main.c
 */

//Forward Delcarations
//Drivers and Headers
#include "inc/hw_types.h"
#include "inc/hw_adc.h"
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

#define BUTTON_CLOCK 200 // button scanning interrupt rate in Hz

unsigned long g_ulSystemClock; // system clock frequency in Hz

//Width of Screen in Pixels
#define FRAME_SIZE_Y 128
//Height of Screen in Pixels
#define FRAME_SIZE_X 96


//ADC Variables
#define ADC_BUFFER_SIZE 2048//size of the buffer(power of 2)
#define ADC_BUFFER_WRAP(i) ((i) & (ADC_BUFFER_SIZE - 1))//index wrapping macro
volatile int g_iADCBufferIndex = ADC_BUFFER_SIZE - 1;//latest sample index
volatile unsigned short g_pusADCBuffer[ADC_BUFFER_SIZE];//circular buffer of ADC samples
volatile unsigned long g_ulADCErrors = 0;//ADC missed deadlines
//ADC Bits
#define ADC_BITS 16
#define PIXELS_PER_DIV 12
#define VIN_RANGE 6


//FIFO Variables
#define FIFO_SIZE 11
//Actual FIFO
int g_buttonFifo[FIFO_SIZE];
//Head and Tail
volatile int g_fifo_head = 0;
volatile int g_fifo_tail = 0;

//Button Press Variables
#define SELECT_BUTTON 0x1
#define UP_BUTTON (0x1 << 1)
#define DOWN_BUTTON (0x1 << 2)
#define LEFT_BUTTON (0x1 << 3)
#define RIGHT_BUTTON (0x1 << 4)

//Voltage Scales
const char * const g_ppcVoltageScaleStr[] = {
		"100 mV","200 mV","500 mV","1 V"
};
//CHANGE THIS
//Offset at 0V
#define ADC_OFFSET 16000

int fifo_put(int value);
int fifo_get(int * value);
void configureAdc();
void configureTimerA0();
void configureGpio();

//Trigger State
typedef enum{
	kRisingEdge,
	kFallingEdge
}TriggerState;

int main(void) {
	// initialize the clock generator
	if (REVISION_IS_A2)
	{
		SysCtlLDOSet(SYSCTL_LDO_2_75V);
	}

	SysCtlClockSet(SYSCTL_SYSDIV_4 | SYSCTL_USE_PLL | SYSCTL_OSC_MAIN |
			SYSCTL_XTAL_8MHZ);
	g_ulSystemClock = SysCtlClockGet();

	//Configure the Peripherals
	configureAdc();
	configureTimerA0();
	configureGpio();


	RIT128x96x4Init(3500000); // initialize the OLED display

	//Local Variables
	// Start off as a rising edge
	TriggerState t = kRisingEdge;

	IntMasterEnable();

	while(1){
		int startingIndex = ADC_BUFFER_WRAP(g_iADCBufferIndex - (FRAME_SIZE_Y / 2));
		int firstStart = startingIndex;
		short finished = 0;
		while(!finished){
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
			if(abs(startingIndex - finishStart) > (ADC_BUFFER_SIZE / 2)){//Can't Find Trigger
				finished = 1;
				startingIndex = firstStart;
				break;
			}





			//Decrement the index
			startingIndex = prevIndex;
		}
		//Now copy to local buffer
		unsigned int i;
		short tempBuffer[FRAME_SIZE_Y];
		//use a for loop
		for(i = 0; i < FRAME_SIZE_Y; i++){
			tempBuffer[i] = g_pusADCBuffer[ADC_BUFFER_WRAP(startingIndex - (FRAME_SIZE_Y / 2) + i)];
		}
	}

	return 0;
}



/*!
 * Put a value into the FIFO
 * @return
 * 	1 on successful put, 0 on full FIFO
 */
int fifo_put(int value){
	int new_tail = g_fifo_tail + 1;
	//Wrap Around
	if(new_tail >= FIFO_SIZE) new_tail = 0;
	//Check if FIFO is full
	if(new_tail != g_fifo_tail){
		//Store in FIFO
		g_buttonFifo[new_tail] = value;
		//Advance the Tail
		g_fifo_tail = new_tail;
		return 1;
	}
	//FIFO Full
	return 0;
}
/*!
 * Retrieve a Value from the FIFO
 * @return
 * 	1 on sucess, 0 on no data
 */
int fifo_get(int * value){
	//FIFO not empty
	if(g_fifo_head != g_fifo_tail){
		//Extract Value from FIFO
		*value = g_buttonFifo[g_fifo_head];
		//Wrap
		if (g_fifo_head + 1 >= FIFO_SIZE)
			g_fifo_head = 0;
		else
			g_fifo_head++;//Push forward the head
		//we have data
		return 1;
	}
	//Nothing in FIFO
	return 0;
}

//ISR for ADC conversions
//Highest Priority ISR
void ADC_ISR(void){
	ADC0_ISC_R = ADC_ISC_IN0; // clear ADC sequence0 interrupt flag in the ADCISC
	//ADC overflow checking
	if(ADC0_OSTAT_R & ADC_OSTAT_OV0){
		g_ulADCErrors++;
		ADC0_OSTAT_R = ADC_OSTAT_OV0;
	}

	int buffer_index = ADC_BUFFER_WRAP(g_iADCBufferIndex + 1);
	//Read from ADC
	g_pusADCBuffer[buffer_index] = ADC0_SSFIFO0_R;
	g_iADCBufferIndex = buffer_index;
}

//ISR for timer 0
//Primary purpose to read the button inputs and place into FIFO for
//main function to pick up
//Runs at a medium priority
void TIMER_0_ISR(){
	//Read buttons and debounce and place in FIFO
	unsigned long presses = g_ulButtons;
	TIMER0_ICR_R = TIMER_ICR_TATOCINT; // clear interrupt flag

	//Debounce the Buttons
	ButtonDebounce(((~GPIO_PORTF_DATA_R & GPIO_PIN_1) >> 1) |
			((~GPIO_PORTE_DATA_R & (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3)) << 1));
	//get the presses
	presses = ~presses & g_ulButtons; // button presses

	if(presses & SELECT_BUTTON){
		fifo_put(SELECT_BUTTON);
	}
	if(presses & UP_BUTTON){
		fifo_put(UP_BUTTON);
	}
	if(presses & DOWN_BUTTON){
		fifo_put(DOWN_BUTTON);
	}
	if(presses & LEFT_BUTTON){
		fifo_put(LEFT_BUTTON);
	}
	if(presses & RIGHT_BUTTON){
		fifo_put(RIGHT_BUTTON);
	}

}
/*!
 * Configures the ADC
 */
void configureAdc(){
	unsigned int i;
	//Clear ADC Buffer
	for(i = 0; i < ADC_BUFFER_SIZE; i++){
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
	ADCSequenceConfigure(ADC0_BASE,0,ADC_TRIGGER_ALWAYS, 0);
	//Configure the Sequence Step
	ADCSequenceStepConfigure(ADC0_BASE, 0, 0,
			ADC_CTL_IE | ADC_CTL_END | ADC_CTL_CH0);
	//Enable ADC Interrupt from Sequence 0
	ADCIntEnable(ADC0_BASE, 0);
	//Enabled ADC interrupt from sequence 0
	ADCSequenceEnable(ADC0_BASE, 0);
	//Enable the Interrupts
	IntPrioritySet(INT_ADC0SS0, 0);
	IntEnable(INT_ADC0SS0);

}
//Configure TimerA0
void configureTimerA0(){


	unsigned long ulDivider, ulPrescaler;
	// initialize a general purpose timer for periodic interrupts￼￼￼
	SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
	TimerDisable(TIMER0_BASE, TIMER_BOTH);
	TimerConfigure(TIMER0_BASE, TIMER_CFG_SPLIT_PAIR | TIMER_CFG_A_PERIODIC); // prescaler for a 16-bit timer
	ulPrescaler = (g_ulSystemClock / BUTTON_CLOCK - 1) >> 16;
	// 16-bit divider (timer load value)
	ulDivider = g_ulSystemClock / (BUTTON_CLOCK * (ulPrescaler + 1)) - 1;
	TimerLoadSet(TIMER0_BASE, TIMER_A, ulDivider);
	TimerPrescaleSet(TIMER0_BASE, TIMER_A, ulPrescaler);

	//Interrupts
	TimerIntEnable(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
	TimerEnable(TIMER0_BASE, TIMER_A);

	//Give it a medium priority
	IntPrioritySet(INT_TIMER0A, 32); // 0 = highest priority, 32 = next lower IntEnable(INT_TIMER0A);

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

