/* Standard includes. */
#include <stdint.h>
#include <stdio.h>
#include "stm32f4_discovery.h"
/* Kernel includes. */
#include "stm32f4xx.h"
#include "../FreeRTOS_Source/include/FreeRTOS.h"
#include "../FreeRTOS_Source/include/queue.h"
#include "../FreeRTOS_Source/include/semphr.h"
#include "../FreeRTOS_Source/include/task.h"
#include "../FreeRTOS_Source/include/timers.h"

/*-----------------------------------------------------------*/
#define mainQUEUE_LENGTH 100
#define MAX_LEDS 19

#define amber  	2
#define green  	1
#define red  	3
#define blue  	4

#define amber_led	LED3
#define green_led	LED4
#define red_led    	LED5
#define blue_led	LED6


/*
 * TODO: Implement this function for any hardware specific clock configuration
 * that was not already performed before main() was called.
 */
static void prvSetupHardware( void );

/*
 * The queue send and receive tasks as described in the comments at the top of
 * this file.
 */
static void Manager_Task( void *pvParameters );
static void vTaskADC( void *pvParameters );
static void vTaskTrafficLight( void *pvParameters );
static void vTaskTrafficFlow( void *pvParameters );
static void vTaskCarGenerator(void *pvParameters);

TimerHandle_t amberLightTimerHandle;
TimerHandle_t greenLightTimerHandle;
TimerHandle_t redLightTimerHandle;

xQueueHandle xQueue_handle = 0;

// Project 1
void myGPIOC_Init( void );
void myADC_Init( void );
uint16_t getADC( void );
void test_LEDs(void);
void sendToShiftRegisters( uint32_t );

void amberLightTimerCallback(TimerHandle_t xTimer);
void greenLightTimerCallback(TimerHandle_t xTimer);
void redLightTimerCallback(TimerHandle_t xTimer);

uint16_t carArray[MAX_LEDS] = {0};
uint16_t trafficRate = 1;
uint16_t lightState = green;

xQueueHandle xQueue_ADC;
xQueueHandle xQueue_LightState;
xQueueHandle xQueue_Buffer;


/*-----------------------------------------------------------*/

int main(void)
{

//	/* Initialize LEDs */
	STM_EVAL_LEDInit(amber_led);
	STM_EVAL_LEDInit(green_led);
	STM_EVAL_LEDInit(red_led);
	STM_EVAL_LEDInit(blue_led);


	/* Configure the system ready to run the demo.  The clock configuration
	can be done here if it was not done before main() was called. */
//	prvSetupHardware();
//
//
//	/* Create the queue used by the queue send and queue receive tasks.
//	http://www.freertos.org/a00116.html */
   xQueue_handle = xQueueCreate( 	mainQUEUE_LENGTH,    	/* The number of items the queue can hold. */
                        	sizeof( uint16_t ) );	/* The size of each item the queue holds. */
//
//	/* Add to the registry, for the benefit of kernel aware debugging. */
	vQueueAddToRegistry( xQueue_handle, "MainQueue" );
//

//
//	/* Start the tasks and timer running. */

	myGPIOC_Init();
	myADC_Init();

	GPIO_ResetBits(GPIOC, GPIO_Pin_0);

//Create timer for amber, green, and red lights
	amberLightTimerHandle = xTimerCreate("AmberLight", pdMS_TO_TICKS(1000), pdTRUE, NULL, amberLightTimerCallback);
	greenLightTimerHandle = xTimerCreate("GreenLight", pdMS_TO_TICKS(1000), pdTRUE, NULL, greenLightTimerCallback);
	redLightTimerHandle = xTimerCreate("RedLight", pdMS_TO_TICKS(1000), pdTRUE, NULL, redLightTimerCallback);

//Create queues for managing ADC value, the state of the traffic lights, and the traffic buffer
	xQueue_ADC = xQueueCreate(1, sizeof(uint16_t));
	xQueue_LightState = xQueueCreate(1, sizeof(uint16_t));
	xQueue_Buffer = xQueueCreate(10, sizeof(uint16_t));

	// if (xQueue_ADC != NULL){

//Create the tasks for getting the ADC, managing the traffic light, managing the flow of traffic using 19 green LEDs, and traffic generator
	xTaskCreate( vTaskADC, "ADC Reader", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
	xTaskCreate( vTaskTrafficLight, "TrafficLight", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
	xTaskCreate( vTaskTrafficFlow, "TrafficFlow", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
	xTaskCreate( vTaskCarGenerator, "TrafficGenerator", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

	//Start the scheduler for the tasks
	vTaskStartScheduler();

	uint16_t adc_value;

	while (1) {

    	adc_value = getADC();

	//Print the ADC value for testing/debugging
    	printf("\n ADC Value: %d\n", adc_value);

	}

	return 0;

}

/*-----------------------------------------------------------*/
/*
Function - myGPIOC_Init
Initializes GPIOC
*/
void myGPIOC_Init() {

	// Enable clock for GPIOC
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);

	// Initialize the GPIO Structure
	GPIO_InitTypeDef GPIO_InitStruct;

	// Configure PC0 (Red LED), PC1 (Amber LED), PC2 (Green LED) as outputs
	GPIO_InitStruct.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOC, &GPIO_InitStruct);

	// Configure PC3 (Potentiometer) as input (analog for ADC)
	GPIO_InitStruct.GPIO_Pin = GPIO_Pin_3;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AN;
	GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOC, &GPIO_InitStruct);

	// Configure Shift Register PC6 (Data), PC7 (Clock), and PC8 (Reset) as output
	GPIO_InitStruct.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7 | GPIO_Pin_8;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOC, &GPIO_InitStruct);

}

/*-----------------------------------------------------------*/
/*
Function - myADC_Init
Initializes ADC
*/
void myADC_Init() {

	// Enable the clock for the ADC
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);

	// Initialize the ADC and its structure
	ADC_InitTypeDef ADC_InitStruct;
	ADC_InitStruct.ADC_Resolution = ADC_Resolution_12b;
	ADC_InitStruct.ADC_ScanConvMode = DISABLE;
	ADC_InitStruct.ADC_ContinuousConvMode = DISABLE; //
	ADC_InitStruct.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_None;
	ADC_InitStruct.ADC_DataAlign = ADC_DataAlign_Right;
	ADC_InitStruct.ADC_NbrOfConversion = 1;
	ADC_Init(ADC1, &ADC_InitStruct);

	// Enable the ADC
	ADC_Cmd(ADC1, ENABLE);

	// Configure the ADC Channel
	ADC_RegularChannelConfig(ADC1, ADC_Channel_13, 1, ADC_SampleTime_3Cycles);

}
/*-----------------------------------------------------------*/
/*
Function - amberLightTimerCallback
When the timer for the amber light runs out:
Changes the traffic light state variable to red
Retrieves the most recent ADC value from the ADC queue and saves it to a local variable
Stops all timers related to the traffic lights to ensure synchronization
Turns off the amber traffic light and turns on the red traffic light
Updates the traffic light state in the traffic light queue
Updates the length of time for the red traffic light timer and begins that timer
*/
void amberLightTimerCallback(TimerHandle_t xTimer){

	//change traffic light state to red 
	lightState = red;

	//Retrieve the most recent ADC value from the ADC queue and saves it to a local variable
	uint16_t adcValue;
	xQueuePeek(xQueue_ADC, &adcValue, pdMS_TO_TICKS(50));

	//For debugging and testing purposes
	//printf("turn off amber\n");

	xTimerStop(greenLightTimerHandle, 0);
	xTimerStop(amberLightTimerHandle, 0);
	xTimerStop(redLightTimerHandle, 0);

	//Turn off the green traffic light and turns on the amber traffic light
	GPIO_ResetBits(GPIOC, GPIO_Pin_1);
	GPIO_SetBits(GPIOC, GPIO_Pin_0);

	//Update the traffic light state in the traffic light queue
	xQueueOverwrite(xQueue_LightState, &lightState);

	//Update the length of time for the red traffic light timer and begins that timer
	xTimerChangePeriod(redLightTimerHandle, pdMS_TO_TICKS(((4095-adcValue)/4095)*2000 + 2000), 0);

}

/*-----------------------------------------------------------*/
/*
Function - greenLightTimerCallback
When the timer for the amber light runs out:
Changes the traffic light state variable to amber
Stops all timers related to the traffic lights to ensure synchronization
Turns off the green traffic light and turns on the amber traffic light
Updates the traffic light state in the traffic light queue
Begins the amber traffic light timer
*/
void greenLightTimerCallback(TimerHandle_t xTimer){
	
	//Change the traffic light state to amber
	lightState = amber;

	//Stop all timers to ensure synchornization
	xTimerStop(greenLightTimerHandle, 0);
	xTimerStop(amberLightTimerHandle, 0);
	xTimerStop(redLightTimerHandle, 0);

	//uint16_t adcValue;
	//xQueuePeek(xQueue_ADC, &adcValue, pdMS_TO_TICKS(50));

	//Turn off the green light and turn on the amber light
	GPIO_ResetBits(GPIOC, GPIO_Pin_2);
	GPIO_SetBits(GPIOC, GPIO_Pin_1);
	
	//Start the amber traffic light timer
	xQueueOverwrite(xQueue_LightState, &lightState);
	if (amberLightTimerHandle != NULL){
    	xTimerStart(amberLightTimerHandle, 0);
	}
}

/*-----------------------------------------------------------*/
/*
Function - redLightTimerCallback
When the timer for the amber light runs out:
Changes the traffic light state variable to green
Retrieves the most recent ADC value from the ADC queue and saves it to a local variable
Stops all timers related to the traffic lights to ensure synchronization
Turns off the red traffic light and turns on the green traffic light
Updates the traffic light state in the traffic light queue
Updates the length of time for the green traffic light timer and begins that timer
*/

void redLightTimerCallback(TimerHandle_t xTimer){

	//Update traffic light state to green
	lightState = green;

	//Stop all traffic light timers to ensure synchroniztion
	xTimerStop(greenLightTimerHandle, 0);
	xTimerStop(amberLightTimerHandle, 0);
	xTimerStop(redLightTimerHandle, 0);

	//Retrieve most recent ADC value and store it in local ADC variable
	uint16_t adcValue;
	xQueuePeek(xQueue_ADC, &adcValue, pdMS_TO_TICKS(50));

	//Turn off the red traffic light and turn on the green traffic light
	GPIO_ResetBits(GPIOC, GPIO_Pin_0);
	GPIO_SetBits(GPIOC, GPIO_Pin_2);

	//printf("time: %ud", ((4095-adcValue)/4095)*2000 + 2000);

	//Update the traffic light state in the traffic light state queue
	xQueueOverwrite(xQueue_LightState, &lightState);

	//Update the length of the green traffic light timer length and start the timer
	xTimerChangePeriod(greenLightTimerHandle, pdMS_TO_TICKS((adcValue/4095)*2000) + 2000 , 0);

}

/*-----------------------------------------------------------*/
/*
Function - getADC
Retrieve and return ADC value
*/
uint16_t getADC(void) {

	ADC_ClearFlag(ADC1, ADC_FLAG_EOC);
	ADC_SoftwareStartConv(ADC1);

	while(!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC));

	return ADC_GetConversionValue(ADC1);
}

/*-----------------------------------------------------------*/
/*
Task - vTaskADC
Take ADC value using getADC function and update the ADC value in the ADC queue every 100ms
BUG: ADC value retrieved in getADC function is 16 times larger than correct ADC value. ADC Value also appears to be sensitive to some kind of noise so the calculated value is +/-40 the correct value.
FIX: Divide ADC value by 16.
*/

void vTaskADC(void *pvParameters) {

	uint16_t adcValue;

	while(1){
    	adcValue = getADC() / 16;
    	xQueueOverwrite(xQueue_ADC, &adcValue);
    	vTaskDelay(100);
	}
}

/*-----------------------------------------------------------*/

static void Manager_Task( void *pvParameters )
{
	uint16_t tx_data = amber;

	while(1)
	{

    	if(tx_data == amber)
        	STM_EVAL_LEDOn(amber_led);
    	if(tx_data == green)
        	STM_EVAL_LEDOn(green_led);
    	if(tx_data == red)
        	STM_EVAL_LEDOn(red_led);
    	if(tx_data == blue)
        	STM_EVAL_LEDOn(blue_led);

    	if( xQueueSend(xQueue_handle,&tx_data,1000))
    	{
        	printf("Manager: %u ON!\n", tx_data);
        	if(++tx_data == 4)
            	tx_data = 0;
        	vTaskDelay(1000);
    	}
    	else
    	{
        	printf("Manager Failed!\n");
    	}
	}
}

/*-----------------------------------------------------------*/

void vApplicationMallocFailedHook( void )
{
	/* The malloc failed hook is enabled by setting
	configUSE_MALLOC_FAILED_HOOK to 1 in FreeRTOSConfig.h.

	Called if a call to pvPortMalloc() fails because there is insufficient
	free memory available in the FreeRTOS heap.  pvPortMalloc() is called
	internally by FreeRTOS API functions that create tasks, queues, software
	timers, and semaphores.  The size of the FreeRTOS heap is set by the
	configTOTAL_HEAP_SIZE configuration constant in FreeRTOSConfig.h. */
	for( ;; );
}
/*-----------------------------------------------------------*/

void vApplicationStackOverflowHook( xTaskHandle pxTask, signed char *pcTaskName )
{
	( void ) pcTaskName;
	( void ) pxTask;

	/* Run time stack overflow checking is performed if
	configconfigCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2.  This hook
	function is called if a stack overflow is detected.  pxCurrentTCB can be
	inspected in the debugger if the task name passed into this function is
	corrupt. */
	for( ;; );
}
/*-----------------------------------------------------------*/

void vApplicationIdleHook( void )
{
volatile size_t xFreeStackSpace;

	/* The idle task hook is enabled by setting configUSE_IDLE_HOOK to 1 in
	FreeRTOSConfig.h.

	This function is called on each cycle of the idle task.  In this case it
	does nothing useful, other than report the amount of FreeRTOS heap that
	remains unallocated. */
	xFreeStackSpace = xPortGetFreeHeapSize();

	if( xFreeStackSpace > 100 )
	{
    	/* By now, the kernel has allocated everything it is going to, so
    	if there is a lot of heap remaining unallocated then
    	the value of configTOTAL_HEAP_SIZE in FreeRTOSConfig.h can be
    	reduced accordingly. */
	}
}
/*-----------------------------------------------------------*/

static void prvSetupHardware( void )
{
	/* Ensure all priority bits are assigned as preemption priority bits.
	http://www.freertos.org/RTOS-Cortex-M3-M4.html */
	NVIC_SetPriorityGrouping( 0 );

	/* TODO: Setup the clocks, etc. here, if they were not configured before
	main() was called. */
}
/*-----------------------------------------------------------*/
/*
Task - vTaskTrafficLight
Turns off all traffic lights to ensure synchronization, turns on the green traffic light, and begins green traffic light timer.
*/
static void vTaskTrafficLight( void *pvParameters )
{

	uint16_t tx_data = green;
	uint16_t adcValue;

	//turn off all lights and turn on green light
	GPIO_ResetBits(GPIOC, GPIO_Pin_0);
	GPIO_ResetBits(GPIOC, GPIO_Pin_1);
	GPIO_ResetBits(GPIOC, GPIO_Pin_2);
	GPIO_SetBits(GPIOC, GPIO_Pin_2);

	xQueuePeek(xQueue_ADC, &adcValue, pdMS_TO_TICKS(50));
	printf("\nADC:%d\n", adcValue);


	if (greenLightTimerHandle != NULL){
    	xTimerStart(greenLightTimerHandle, 0);
	}


	while(1){
    	//adcValue = getADC();
    	/*xQueuePeek(xQueue_ADC, &adcValue, pdMS_TO_TICKS(50));
    	printf("\nADC:%d\n", adcValue);

    	//greenMult = adcValue/4095;
    	//redMult = adcValue/4095;

    	if (greenLightTimerHandle != NULL){
        	xTimerStart(greenLightTimerHandle, 0);
    	}
*/
    	/*switch(tx_data){
    	case green:
        	printf("\nTurn Green\n");
        	if (greenLightTimerHandle != NULL){
            	xTimerStart(greenLightTimerHandle, 0);
        	}
        	//vTaskDelay(greenMult*5000+5000);
        	xQueueOverwrite(xQueue_LightState, &tx_data);
        	break;

    	case amber:
        	if(amberLightTimerHandle != NULL){
            	xTimerStart(amberLightTimerHandle, 0);
        	}
        	GPIO_SetBits(GPIOC, GPIO_Pin_1);
        	vTaskDelay(1000);
        	GPIO_ResetBits(GPIOC, GPIO_Pin_1);
        	xQueueOverwrite(xQueue_LightState, &tx_data);
        	break;

    	case red:
        	printf("\nTurn Red\n");
        	if(redLightTimerHandle != NULL){
            	xTimerStart(redLightTimerHandle, 0);
        	}
        	GPIO_SetBits(GPIOC, GPIO_Pin_0);
        	vTaskDelay(redMult*5000+2000);
        	GPIO_ResetBits(GPIOC, GPIO_Pin_0);
        	xQueueOverwrite(xQueue_LightState, &tx_data);
        	break;
    	}*/

    	if( xQueueSend(xQueue_handle,&tx_data,1000)){
        	if(++tx_data == 4)
            	tx_data = 1;
    	}

	}
}
/*-----------------------------------------------------------*/
/*
Function - sendToShiftRegisters
Bit shifts the value of the traffic state and sends the updated value to the shift registers.
*/
void sendToShiftRegisters(uint32_t state) {

	GPIO_ResetBits(GPIOC, GPIO_Pin_8); // Reset shift registers
//	vTaskDelay(200);
	GPIO_SetBits(GPIOC, GPIO_Pin_8);  // End reset

	for (int i = 0; i < 19; i++) {  // 19 LEDs total
    	// Set data bit
    	if (state & (1 << (18 - i))) {
        	GPIO_SetBits(GPIOC, GPIO_Pin_6);
    	} else {
        	GPIO_ResetBits(GPIOC, GPIO_Pin_6);
    	}

    	// Clock pulse (falling edge)
    	GPIO_SetBits(GPIOC, GPIO_Pin_7);
    	for (int i = 1; i < 10; i++);
    	GPIO_ResetBits(GPIOC, GPIO_Pin_7);
	}
}
/*-----------------------------------------------------------*/
/*
Task - vTaskTrafficFlow
Uses a representation of the traffic state value in an array called carArray.  Checks the light state in the traffic light state queue; if the light is green, the ‘cars’ move normally, if the light is red or amber, the cars past the traffic light move normally and the cars before the light stop before the traffic light or behind the stopped in front of it, whichever comes first.
*/
static void vTaskTrafficFlow(void *pvParameters) {

	//printf("WORKING\n");

	//uint32_t traffic = 0x1; // Start with the first LED ON
	uint16_t light;

	while (1) {

    	if (xQueuePeek(xQueue_LightState, &light, pdMS_TO_TICKS(50)) == pdPASS){
        
	//Moves cars over by one when light is green
if (light == green){
            	for (int i = MAX_LEDS - 1; i > 0; i--){
                	carArray[i] = carArray[i-1];
            	}

	//Makes cars stop at red light or before car in front of them if they are before the red light
        	} else {

            	for(int i = 10; i > 0; i--){
                	if( carArray[i] == 0){
                    	carArray[i] = carArray[i-1];
                    	carArray[i-1] = 0;
                	}

            	}

            	for (int i = MAX_LEDS - 1; i > 11; i --){
                	carArray[i] = carArray[i-1];
                	carArray[i-1] = 0;

            	}
        	}


	//While there is traffic in the buffer, add a new car to the start of the lights on the board
        	uint16_t newCar = 1;
        	if (xQueueReceive(xQueue_Buffer, &newCar, 0) == pdPASS) {
            	carArray[0] = newCar;
        	}

        	uint32_t trafficState = 0;
        	for (int i = 0; i < MAX_LEDS; i++) {
            	if (carArray[i]) {
                	trafficState |= (1 << i);
            	}
        	}

        	sendToShiftRegisters(trafficState);
    	}
    	vTaskDelay(pdMS_TO_TICKS(1000));

	}
}
/*-----------------------------------------------------------*/
/*
Task - vTaskCarGenerator
Creates new cars based on the ADC value and adds them to the traffic queue
*/
void vTaskCarGenerator(void *pvParameters) {

	uint16_t spawnRate = 1;
	while (1) {

	// Get the most recent ADC value from the queue and store it as local variable ‘trafficRate’
    	xQueuePeek(xQueue_ADC, &trafficRate, pdMS_TO_TICKS(50));
	
	//Determine the intended spawn rate given the new ADC value
    	spawnRate = ((4095 - trafficRate) / 4095.0) * 5000 + 1000;

	//Adds the new car to the traffic queue
    	if (uxQueueSpacesAvailable(xQueue_Buffer) > 0) {
        	uint8_t newCar = 1;
        	xQueueSend(xQueue_Buffer, &newCar, 0);
    	}

	//Wait for the amount of time specified by spawnRate in ms before generating more traffic
    	vTaskDelay(pdMS_TO_TICKS(spawnRate));
	}
}


