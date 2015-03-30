/*
 * main.c
 */

//Drivers and Headers
#include "inc/hw_types.h"
#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"
#include "inc/hw_sysctl.h"
#include "inc/lm3s8962.h"
#include "driverlib/sysctl.h"

#include "frame_graphics.h"
#include "utils/ustdlib.h"

#include "drivers/rit128x96x4.h"

#include "driverlib/timer.h"
#include "driverlib/interrupt.h"

#include "driverlib/gpio.h"
#include "buttons.h"

#define BUTTON_CLOCK 200 // button scanning interrupt rate in Hz

unsigned long g_ulSystemClock; // system clock frequency in Hz

//SIN X COORDINATES
const static unsigned char sin_x_coords[]={
		111,
		110,
		109,
		107,
		105,
		102,
		99,
		95,
		92,
		88,
		83,
		79,
		74,
		69,
		64,
		59,
		54,
		49,
		45,
		41,
		36,
		33,
		29,
		26,
		23,
		21,
		19,
		18,
		17,
		17,
		17,
		18,
		19,
		21,
		23,
		26,
		29,
		33,
		36,
		41,
		45,
		49,
		54,
		59,
		64,
		69,
		74,
		79,
		83,
		88,
		92,
		95,
		99,
		102,
		105,
		107,
		109,
		110,
		111,
		111
};

const static unsigned char sin_y_coords[]={
		53,
		58,
		63,
		67,
		72,
		76,
		79,
		83,
		86,
		89,
		91,
		93,
		94,
		95,
		95,
		95,
		94,
		93,
		91,
		89,
		86,
		83,
		79,
		76,
		72,
		67,
		63,
		58,
		53,
		48,
		43,
		38,
		33,
		29,
		25,
		20,
		17,
		13,
		10,
		7,
		5,
		3,
		2,
		1,
		1,
		1,
		2,
		3,
		5,
		7,
		10,
		13,
		17,
		20,
		25,
		29,
		33,
		38,
		43,
		48
};

volatile unsigned long g_ulTime;

void TimerISR(void) {
	static int tic = false;
	static int running = true;
	unsigned long presses = g_ulButtons;
	TIMER0_ICR_R = TIMER_ICR_TATOCINT; // clear interrupt flag
	//ButtonDebounce((~GPIO_PORTF_DATA_R & ~GPIO_PORTE_DATA_R & GPIO_PIN_0 & GPIO_PIN_1 & GPIO_PIN_2 & GPIO_PIN_3) >> 1);
	//Debounce both bitfields at once and arrange into a combined five-bit field
	ButtonDebounce(((~GPIO_PORTF_DATA_R & GPIO_PIN_1) >> 1) | ((~GPIO_PORTE_DATA_R & (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3))<<1));
	presses = ~presses & g_ulButtons; // button press detector
	if (presses & 1) { // "select" button pressed
		running = !running;
	}


	if (running) {
		if (tic) {
			g_ulTime++; // increment time every other ISR call
			tic = false;
		}
		else
			tic = true;
	}
	//If any of the four buttons are pressed reset time
	if((presses >> 1)){
		g_ulTime = 0;
	}
}
unsigned long ulDivider, ulPrescaler;

int main(void) {
	g_ulTime = 0;

	// configure GPIO used to read the state of the on-board push buttons
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
	GPIOPinTypeGPIOInput(GPIO_PORTF_BASE, GPIO_PIN_1);
	GPIOPadConfigSet(GPIO_PORTF_BASE, GPIO_PIN_1, GPIO_STRENGTH_2MA,
			GPIO_PIN_TYPE_STD_WPU);
	//Set up four button GPIO
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
	GPIOPinTypeGPIOInput(GPIO_PORTE_BASE, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3);
	GPIOPadConfigSet(GPIO_PORTE_BASE, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3, GPIO_STRENGTH_2MA,
			GPIO_PIN_TYPE_STD_WPU);


	// initialize the clock generator
	if (REVISION_IS_A2)
	{
		SysCtlLDOSet(SYSCTL_LDO_2_75V);
	}
	SysCtlClockSet(SYSCTL_SYSDIV_4 | SYSCTL_USE_PLL | SYSCTL_OSC_MAIN |
			SYSCTL_XTAL_8MHZ);
	g_ulSystemClock = SysCtlClockGet();

	RIT128x96x4Init(3500000); // initialize the OLED display

	//volatile unsigned long g_ulTime = 8345; // time in hundredths of a second
	char pcStr[50]; // string buffer
	unsigned long ulTime; // local copy of g_ulTime

	// initialize a general purpose timer for periodic interrupts
	SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
	TimerDisable(TIMER0_BASE, TIMER_BOTH);
	TimerConfigure(TIMER0_BASE, TIMER_CFG_SPLIT_PAIR | TIMER_CFG_A_PERIODIC);
	// prescaler for a 16-bit timer
	ulPrescaler = (g_ulSystemClock / BUTTON_CLOCK - 1) >> 16;
	// 16-bit divider (timer load value)
	ulDivider = g_ulSystemClock / (BUTTON_CLOCK * (ulPrescaler + 1)) - 1; TimerLoadSet(TIMER0_BASE, TIMER_A, ulDivider);
	TimerPrescaleSet(TIMER0_BASE, TIMER_A, ulPrescaler);
	TimerIntEnable(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
	TimerEnable(TIMER0_BASE, TIMER_A);
	// initialize interrupt controller to respond to timer interrupts
	//	IntRegister(INT_TIMER0A, TimerISR);

	IntPrioritySet(INT_TIMER0A, 0); // 0 = highest priority, 32 = next lower
	IntEnable(INT_TIMER0A);
	IntMasterEnable();

	while (true) {
		FillFrame(0); // clear frame buffer
		ulTime = g_ulTime; // read volatile global only once

		unsigned int i;
		//Get the hundreths, sec and minutes
		unsigned long hundreths = (ulTime)%100;
		unsigned long seconds = (ulTime/100)%60;
		unsigned long minutes = (ulTime/6000)%60;
//		usprintf(pcStr, "%02u:%02u:%02u", minutes,seconds,hundreths); // convert time to string
//		DrawString(0, 0, pcStr, 8, false); // draw string to frame buffer
		for(i = 0; i < 60; i++){
			if((i%5==4)){
				DrawPoint(sin_x_coords[i], sin_y_coords[i],15);
			}else{
				DrawPoint(sin_x_coords[i], sin_y_coords[i],8);
			}

		}

		//Seconds
		DrawLine(64,48,sin_x_coords[seconds],sin_y_coords[seconds], 8);
		//Minutes
		DrawLine(64,48,sin_x_coords[minutes],sin_y_coords[minutes], 15);

//		DrawCircle(64, 48,47,5);
		// copy frame to the OLED screen
		RIT128x96x4ImageDraw(g_pucFrame, 0, 0, FRAME_SIZE_X, FRAME_SIZE_Y);
	}

	return 0;
}
