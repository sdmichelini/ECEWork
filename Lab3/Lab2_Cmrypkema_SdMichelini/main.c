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

#include "kiss_fft.h"
#include "_kiss_fft_guts.h"

#include "network.h"

/*
 * KISS FFT Constants
 */
#define PI 3.14159265358979
//Size of Buffer
#define NFFT 1024
//From Kiss FFT examples
#define KISS_FFT_CFG_SIZE (sizeof(struct kiss_fft_state)+sizeof(kiss_fft_cpx)*(NFFT-1))

/*
 * Kiss FFT Variabless
 */
static char kiss_fft_cfg_buffer[KISS_FFT_CFG_SIZE];//Kiss FFT config memory
size_t buffer_size = KISS_FFT_CFG_SIZE;
kiss_fft_cfg cfg;
static kiss_fft_cpx in[NFFT], out[NFFT];

//ADC Variables
#define ADC_BUFFER_SIZE 2048//size of the buffer(power of 2)
#define ADC_BUFFER_WRAP(i) ((i) & (ADC_BUFFER_SIZE - 1))//index wrapping macro
volatile int g_iADCBufferIndex = ADC_BUFFER_SIZE - 1; //latest sample index
volatile unsigned short g_pusADCBuffer[ADC_BUFFER_SIZE]; //circular buffer of ADC samples
volatile unsigned long g_ulADCErrors = 0; //ADC missed deadlines
//ADC Bits
#define ADCBITCNT 10 //10 bit ADC
#define PIXELCNT 12//Pixels
#define VIN 6 //In V

//ADC Offset
#define ADC_OFFSET 510

//Waveform Buffer
short g_localBuffer[FRAME_SIZE_X];
//Spectrum Buffer
float g_spectrumBuffer[FRAME_SIZE_X];

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

//Size of Grid for FFT
#define FFT_GRID_SIZE 20
#define FFT_TOP_GRID_HEIGHT 10
//FFT display offset
#define DISPLAY_OFFSET 48.0f
//How bright objects are on screen
#define GRID_BRIGHTNESS 0x5
#define TEXT_BRIGHTNESS 0xf

//Trigger State
//What type of trigger it is
typedef enum {
	kRisingEdge, kFallingEdge
} TriggerState;

//Possible States for the Waveform to be in
typedef enum{
	kNormal, kFft
}WaveformState;

//User Interface Variables
TriggerState g_triggerState;
//Menu Selection that is Currently Selected for Editing
short g_editing;
//What voltage div are we on
short g_voltageDiv;
//What scale are we on
short g_scaleDiv;

WaveformState g_waveState;

//Voltage Scales
const char * const g_ppcVoltageScaleStr[] = {
		"100 mV","200 mV","500 mV","1 V"
};
//String representing decibals that we can be at
const char * const g_decibalStrings[]={
		" 5 dBV","10 dBV","20 dBV","40 dBV"
};
//Decibals we can be at
const float g_decibalScale[]={
		5.0f,10.0f,20.0f,40.0f
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
//Doesn't change. Frequency of oscillation
const char * g_frequencyString = "7.1kHz";

/*
 * Configuration Prototypes
 */
void configureAdc(void);
void configureTimerA0();
void configureGpio();
void ADCISR(void);
//Button Tick Callback
void buttonClock(UArg arg0);
//Task to notify UI task of button presses
void buttonTask(UArg arg0, UArg arg1);

//User Input Task
void userInputTask(UArg arg0, UArg arg1);

//Display Task
void displayTask(UArg arg0, UArg arg1);

//Waveform Task
void waveformTask(UArg arg0, UArg arg1);
//FFT task
void fftTask(UArg arg0, UArg arg1);

unsigned long g_ulSystemClock;

/*
 *  ======== main ========
 */
Void main() {

	Error_Block eb;

//	Normal State to Begin With
	g_waveState = kNormal;
	unsigned int i;
//	Zero the Spectrum Buffer
	for(i = 0; i < FRAME_SIZE_X; i++){
		g_spectrumBuffer[i] = 0.0f;
	}

	RIT128x96x4Init(3500000); // initialize the OLED display
	cfg = kiss_fft_alloc(NFFT, 0 , kiss_fft_cfg_buffer, &buffer_size);//Start up Kiss FFT
	//Configure the ADC and GPIO
	configureAdc();
	configureGpio();
	//Init the CAN
	NetworkInit();

	System_printf("enter main()\n");

	Error_init(&eb);
	BIOS_start(); /* enable interrupts and start SYS/BIOS */
}

void ADCISR(void) {
	ADC0_ISC_R = ADC_ISC_IN0; // clear ADC sequence0 interrupt flag in the ADCISC
	//ADC overflow checking
	if (ADC0_OSTAT_R & ADC_OSTAT_OV0) {
		g_ulADCErrors++;
		ADC0_OSTAT_R = ADC_OSTAT_OV0;
	}
//Buffer Index we are inserting into
	int buffer_index = ADC_BUFFER_WRAP(g_iADCBufferIndex + 1);
	while((ADC0_SSFSTAT0_R & ADC_SSFSTAT0_EMPTY) != ADC_SSFSTAT0_EMPTY){
		g_pusADCBuffer[buffer_index] = ADC_SSFIFO0_R & ADC_SSFIFO0_DATA_M; //Read in while we have samples
		buffer_index = ADC_BUFFER_WRAP(buffer_index++);
	}
	
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
	//Configure the Sequence Steps
	//Interrupt on 4 and 8
	ADCSequenceStepConfigure(ADC0_BASE, 0, 0, ADC_CTL_CH0);
	ADCSequenceStepConfigure(ADC0_BASE, 0, 1, ADC_CTL_CH0);
	ADCSequenceStepConfigure(ADC0_BASE, 0, 2, ADC_CTL_CH0);
	ADCSequenceStepConfigure(ADC0_BASE, 0, 3, ADC_CTL_CH0 | ADC_CTL_IE);
	ADCSequenceStepConfigure(ADC0_BASE, 0, 4, ADC_CTL_CH0);
	ADCSequenceStepConfigure(ADC0_BASE, 0, 5, ADC_CTL_CH0);
	ADCSequenceStepConfigure(ADC0_BASE, 0, 6, ADC_CTL_CH0);
	ADCSequenceStepConfigure(ADC0_BASE, 0, 7, ADC_CTL_CH0 | ADC_CTL_IE | ADC_CTL_END);
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
	//What type of message was sent
	char msg;

	while (1) {
		Mailbox_pend(buttonMailbox, &msg, BIOS_WAIT_FOREVER);
		Semaphore_pend(settingsSem, BIOS_WAIT_FOREVER);
		switch(msg){

		case SELECT_BUTTON:
			//Select button pushed
		{
			if(g_waveState == kNormal){
				g_waveState = kFft;
			}else{
				g_waveState = kNormal;
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
		//Done editing settings
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
		WaveformState w = g_waveState;
		Semaphore_post(settingsSem);
		if(w == kNormal){
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
			//done with local buffer
			Semaphore_post(localBufSem);
			Semaphore_post(displaySem);
		}else{
			unsigned int i;
			//FFT
			Semaphore_pend(localBufSem,BIOS_WAIT_FOREVER);
			for(i = 0; i < NFFT; i++){
				in[i].r = g_pusADCBuffer[ADC_BUFFER_WRAP(startingIndex - i - 1)];
				in[i].i = 0;
			}
			Semaphore_post(localBufSem);
			Semaphore_post(fftSem);
		}

	}
}

void fftTask(UArg arg0, UArg arg1){
	while(1){
		//Grab settings
		Semaphore_pend(fftSem, BIOS_WAIT_FOREVER);
		Semaphore_pend(settingsSem, BIOS_WAIT_FOREVER);
		short scaleDiv = g_voltageDiv;
		Semaphore_post(settingsSem);

		kiss_fft(cfg, in, out);
		//Now convert to dB
		unsigned int i = 0;
		for(i = 0 ; i < FRAME_SIZE_X; i++){
			g_spectrumBuffer[i] = (g_decibalScale[scaleDiv] * -1.0f * log10(out[i].r*out[i].r + out[i].i*out[i].i)) + (DISPLAY_OFFSET);
		}

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
		WaveformState w = g_waveState;
		float scale_div = (VIN * PIXELCNT)/((1 << ADCBITCNT) * g_voltageDivArray[voltageDiv]);
		const char * timeScale = g_ppcTimeScaleStr[0];
		short editing = g_editing;

		TriggerState t = g_triggerState;
		Semaphore_post(settingsSem);

		FillFrame(0); // clear frame buffer
		//Draw the points
		//Semaphore_pend(localBufSem, BIOS_WAIT_FOREVER);

		unsigned int i;
		//Draw Code for Waveform
		if(w == kNormal){
			//FFT has different waveform grid
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

			//Draw the Waveform
			for(i = 0; i < FRAME_SIZE_X - 1; i++){
				int y1 = FRAME_SIZE_Y/2 - (int)round((g_localBuffer[i] - ADC_OFFSET) * scale_div);
				int y2 = FRAME_SIZE_Y/2 - (int)round((g_localBuffer[i + 1] - ADC_OFFSET) * scale_div);
				DrawLine(i,y1,i+1,y2,0xf);
			}
		}else{//Draw Code for FFT
			//Grid
			unsigned int p = 0;
			for(p = 1; p <= 5; p++){
				DrawLine(0, -FFT_TOP_GRID_HEIGHT + (p * FFT_GRID_SIZE), FRAME_SIZE_X,  -FFT_TOP_GRID_HEIGHT + (p * FFT_GRID_SIZE), GRID_BRIGHTNESS);
			}
			for(p = 1; p <= 6; p++){
				DrawLine(p * FFT_GRID_SIZE, 0 , p * FFT_GRID_SIZE, FRAME_SIZE_X, GRID_BRIGHTNESS);
			}

			//Menu Code
			switch(editing){
			case EDIT_TIMESCALE:{
				DrawFilledRectangle(5, 3, 40, 10, 0x7);
				break;
			}
			case EDIT_VOLTAGE:{
				DrawFilledRectangle(49, 3, 84, 10, 0x7);
				break;
			}
			default:
				break;
			}
			//Frequency
			DrawString(6, 3, g_frequencyString, 15, 0);

			DrawString(49,3,g_decibalStrings[voltageDiv], 15, 0);

			for(i = 0; i < FRAME_SIZE_X - 1; i++){
				int y1 = (int)g_spectrumBuffer[i];
				int y2 = (int)g_spectrumBuffer[i + 1];
				DrawLine(i,y1,i+1,y2,0xf);
			}
		}
		//Push the Drawing Buffer
		RIT128x96x4ImageDraw(g_pucFrame, 0, 0, FRAME_SIZE_X, FRAME_SIZE_Y);
		//Semaphore_post(localBufSem);
		Semaphore_post(waveformSem);
	}
}

