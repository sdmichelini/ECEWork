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
	configureComparator();
    BIOS_start();     /* enable interrupts and start SYS/BIOS */
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

