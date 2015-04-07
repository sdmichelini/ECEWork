/*
 * main.c
 */

//Forward Delcarations
//Drivers and Headers
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

#define BUTTON_CLOCK 200 // button scanning interrupt rate in Hz

unsigned long g_ulSystemClock; // system clock frequency in Hz


//ADC Variables
#define ADC_BUFFER_SIZE 2048//size of the buffer(power of 2)
#define ADC_BUFFER_WRAP(i) ((i) & (ADC_BUFFER_SIZE - 1))//index wrapping macro
volatile int g_iADCBufferIndex = ADC_BUFFER_SIZE - 1;//latest sample index
volatile unsigned short g_pusADCBuffer[ADC_BUFFER_SIZE];//circular buffer of ADC samples
volatile unsigned long g_ulADCErrors = 0;//ADC missed deadlines
//ADC Bits
#define ADCBITCNT 10 //10 bit ADC
#define PIXELCNT 12
#define VIN 6 //In V


//FIFO Variables
#define FIFO_SIZE 11
//Actual FIFO
char g_buttonFifo[FIFO_SIZE];
//Head and Tail
volatile int g_fifo_head = 0;
volatile int g_fifo_tail = 0;

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

//Voltage Scales
const char * const g_ppcVoltageScaleStr[] = {
		"100 mV","200 mV","500 mV","1 V"
};

//Voltage Settings
float g_voltageDiv[] = {
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
//Offset at 0V
#define ADC_OFFSET 510


//Graph Variables
#define TEXT_BRIGHTNESS 0xf
#define GRID_BRIGHTNESS 0x5

//-----FORWARD DEFINITIONS------//
int fifo_put(char value);
int fifo_get(char * value);
void configureAdc();
void configureAdc_timer();
void setupAdcTimer(unsigned long time);
void configureTimerA0();
void configureCpuTimer();
void configureGpio();
unsigned long cpu_load_count(void);

//Trigger State
//What type of trigger it is
typedef enum{
	kRisingEdge,
	kFallingEdge
}TriggerState;

//CPU load variables
unsigned long loaded;//Ticks for CPU Loaded
unsigned long unloaded;

int main(void) {
	//Disable Interrupts
	IntMasterDisable();
	// initialize the clock generator
	if (REVISION_IS_A2)
	{
		SysCtlLDOSet(SYSCTL_LDO_2_75V);
	}

	SysCtlClockSet(SYSCTL_SYSDIV_4 | SYSCTL_USE_PLL | SYSCTL_OSC_MAIN |
			SYSCTL_XTAL_8MHZ);
	g_ulSystemClock = SysCtlClockGet();

	//Configure the Peripherals
	//	configureAdc();
	//	configureAdc();
	configureAdc_timer();
	setupAdcTimer(24);
	configureTimerA0();
	configureGpio();
	configureCpuTimer();



	RIT128x96x4Init(3500000); // initialize the OLED display

	//Local Variables
	// Start off as a rising edge
	TriggerState t = kRisingEdge;

	unsigned short voltageDiv = 0;

	unloaded = cpu_load_count();
	loaded = 0;
	float cpu_load = 0;

	long loops = 0;

	//Edit the voltage first
	unsigned short editing = EDIT_VOLTAGE;
	unsigned short timerDiv = 0;
	IntMasterEnable();

	float scale_div = (VIN * PIXELCNT)/((1 << ADCBITCNT) * g_voltageDiv[voltageDiv]);


	while(1){

		loops++;

		//GPIO reading from FIFO
		char val = 0;

		//Process Input
		if(fifo_get(&val)){
			switch(val){
			case SELECT_BUTTON://Select button pushed
			{
				if(t == kRisingEdge){//Switch Trigger Edge
					t = kFallingEdge;
				}else{
					t = kRisingEdge;
				}
				break;
			}
			case UP_BUTTON://Up Button Pressed
			{
				if(editing == EDIT_VOLTAGE){
					if(voltageDiv < EDIT_OPTIONS){//Increment Voltage Scale Unless it Reaches it's Max
						voltageDiv++;
						scale_div = (VIN * PIXELCNT)/((1 << ADCBITCNT) * g_voltageDiv[voltageDiv]);//Readjust the Scale
					}
				}else if(editing == EDIT_TIMESCALE){
					if(timerDiv < EDIT_OPTIONS){//Increment Time Scale Unless it Reaches it's Max
						timerDiv++;
						setupAdcTimer(g_timerValues[timerDiv]);//Reconfigure ADC Trigger Timer
						configureAdc_timer();//Reconfigure ADC
					}
				}
				break;
			}
			case DOWN_BUTTON://Down Button Pressed
			{
				if(editing == EDIT_VOLTAGE){
					if(voltageDiv > 0){//Decrement to lowest menu setting
						voltageDiv--;
						scale_div = (VIN * PIXELCNT)/((1 << ADCBITCNT) * g_voltageDiv[voltageDiv]);//Redo Scale
					}
				}else if(editing == EDIT_TIMESCALE){
					if(timerDiv > 0){//Decrement to lowest menu setting
						timerDiv--;
						//Redo ADC and Timer
						setupAdcTimer(g_timerValues[timerDiv]);
						configureAdc_timer();
					}
				}
				break;
			}
			case LEFT_BUTTON://Left Button Pressed
			{
				editing = EDIT_TIMESCALE;//Switch to Time
				break;
			}
			case RIGHT_BUTTON://Right Button Pressed
			{
				editing = EDIT_VOLTAGE;//Switch to Voltage
				break;
			}
			default:

				break;
			}
		}


		//Where we start the search from(a half frame back)
		int startingIndex = ADC_BUFFER_WRAP(g_iADCBufferIndex - (FRAME_SIZE_Y / 2));
		//Where we first started
		int firstStart = startingIndex;
		//Whether or not we are finished
		short finished = 0;
		//How many loops we completed
		int it = 0;
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
		short tempBuffer[FRAME_SIZE_X];
		//use a for loop
		for(i = 0; i < FRAME_SIZE_X; i++){
			tempBuffer[i] = g_pusADCBuffer[ADC_BUFFER_WRAP(startingIndex - (FRAME_SIZE_X / 2) + i)];
		}
		//Now we can draw to the screen
		//First draw the background
		const char * timeScale = g_ppcTimeScaleStr[timerDiv];

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
		DrawString(44,3,g_ppcVoltageScaleStr[voltageDiv], 15, 0);

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

		for(i = 0; i < FRAME_SIZE_X - 1; i++){
			int y1 = FRAME_SIZE_Y/2 - (int)round((tempBuffer[i] - ADC_OFFSET) * scale_div);
			int y2 = FRAME_SIZE_Y/2 - (int)round((tempBuffer[i + 1] - ADC_OFFSET) * scale_div);
			DrawLine(i,y1,i+1,y2,0xf);
		}
		loaded = cpu_load_count();
		//Draw CPu Load
		char pcStr[20]; // string buffer
		cpu_load = 1.0 - (float)loaded/unloaded;
		//Draw the CPU Load
		usprintf(pcStr, "CPU load = %3d.%1d%%", (unsigned long)(cpu_load*100),(unsigned long)(((unsigned long)cpu_load*1000) % 10));
		DrawString(3,85,pcStr, 15, 0);
		//Push the Drawing Buffer
		RIT128x96x4ImageDraw(g_pucFrame, 0, 0, FRAME_SIZE_X, FRAME_SIZE_Y);

	}

	return 0;
}



/*!
 * Put a value into the FIFO
 * @return
 * 	1 on successful put, 0 on full FIFO
 */
int fifo_put(char value){
	int new_tail = g_fifo_tail + 1;
	//Wrap Around
	if(new_tail >= FIFO_SIZE) new_tail = 0;
	//Check if FIFO is full
	if(new_tail != g_fifo_head){
		//Store in FIFO
		g_buttonFifo[g_fifo_tail] = value;
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
int fifo_get(char * value){
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

	//Put the button that was pressed
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
	ADCSequenceConfigure(ADC0_BASE, 0, ADC_TRIGGER_ALWAYS, 0);
	//Configure the Sequence Step
	ADCSequenceStepConfigure(ADC0_BASE, 0, 0,
			ADC_CTL_IE | ADC_CTL_END | ADC_CTL_CH0  );
	//Enable ADC Interrupt from Sequence 0
	ADCIntEnable(ADC0_BASE, 0);
	//Enabled ADC interrupt from sequence 0
	ADCSequenceEnable(ADC0_BASE, 0);
	//Enable the Interrupts
	IntPrioritySet(INT_ADC0SS0, 0x00);
	IntEnable(INT_ADC0SS0);

}

void configureAdc_timer(){
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
	ADCSequenceConfigure(ADC0_BASE, 0, ADC_TRIGGER_TIMER, 0);
	//Configure the Sequence Step
	ADCSequenceStepConfigure(ADC0_BASE, 0, 0,
			ADC_CTL_IE | ADC_CTL_END | ADC_CTL_CH0  );
	//Enable ADC Interrupt from Sequence 0
	ADCIntEnable(ADC0_BASE, 0);
	//Enabled ADC interrupt from sequence 0
	ADCSequenceEnable(ADC0_BASE, 0);
	//Enable the Interrupts
	IntPrioritySet(INT_ADC0SS0, 0x0);
	IntEnable(INT_ADC0SS0);
	//TimerControlTrigger


}
//Configure TimerA0
void configureTimerA0(){


	unsigned long ulDivider, ulPrescaler;
	//need a timer
	SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
	TimerDisable(TIMER0_BASE, TIMER_BOTH);
	TimerConfigure(TIMER0_BASE, TIMER_CFG_SPLIT_PAIR | TIMER_CFG_A_PERIODIC); // prescaler for a 16-bit timer
	ulPrescaler = (g_ulSystemClock / BUTTON_CLOCK - 1) >> 16; //divide then disregard bottom 16 to fit
	ulDivider = g_ulSystemClock / (BUTTON_CLOCK * (ulPrescaler + 1)) - 1;
	TimerLoadSet(TIMER0_BASE, TIMER_A, ulDivider);
	TimerPrescaleSet(TIMER0_BASE, TIMER_A, ulPrescaler);


	//Interrupts
	TimerIntEnable(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
	TimerEnable(TIMER0_BASE, TIMER_A);
	//Give it a medium priority
	IntPrioritySet(INT_TIMER0A, 32); // 0 = highest priority, 32 = next lower IntEnable(INT_TIMER0A);
	IntEnable(INT_TIMER0A);


}

void setupAdcTimer(unsigned long time) {
	//Clock Divider and Prescaler
	unsigned long ul_scaler, ul_DivConst;
	//Desired Frequency
	unsigned long freq = ((12000) / time) * 1000;
	//Enable Peripheral
	SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER1);
	//Disable Interrupt
	TimerIntDisable(TIMER1_BASE, TIMER_TIMA_TIMEOUT);
	//Disable Timer
	TimerDisable(TIMER1_BASE, TIMER_BOTH);
	//Configure Timer for Periodic Interrupts
	TimerConfigure(TIMER1_BASE, TIMER_CFG_SPLIT_PAIR | TIMER_CFG_A_PERIODIC);
	//Set the prescaler and deivers
	ul_scaler = (g_ulSystemClock / freq - 1) >> 16;
	ul_DivConst = g_ulSystemClock / (freq * (ul_scaler + 1)) - 1;
	//Load the Divider
	TimerLoadSet(TIMER1_BASE, TIMER_A, ul_DivConst);
	//Load the prescaler
	TimerPrescaleSet(TIMER1_BASE, TIMER_A, ul_scaler);
	//Enable Interrupts
	TimerIntEnable(TIMER1_BASE, TIMER_TIMA_TIMEOUT);
	TimerControlTrigger(TIMER1_BASE, TIMER_A, true);//Set control trigger
	TimerEnable(TIMER1_BASE, TIMER_A);
}

void configureCpuTimer(){
	// initialize timer 3 in one-shot mode for polled timing
	SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER3);
	TimerDisable(TIMER3_BASE, TIMER_BOTH);
	TimerConfigure(TIMER3_BASE, TIMER_CFG_ONE_SHOT);//One shot mode
	TimerLoadSet(TIMER3_BASE, TIMER_A, (g_ulSystemClock/50) - 1); // 1/50 sec interval

}

unsigned long cpu_load_count(void)
{
	unsigned long idx = 0;
	TimerIntClear(TIMER3_BASE, TIMER_TIMA_TIMEOUT);
	TimerEnable(TIMER3_BASE, TIMER_A); // start one-shot timer
	while (!(TIMER_TIMA_TIMEOUT & TimerIntStatus(TIMER3_BASE, 0)))
		idx++;
	return idx;
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

