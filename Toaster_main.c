// **** Include libraries here ****
// Standard libraries
#include <stdio.h>
#include <stdint.h>

//CSE13E Support Library
#include "BOARD.h"

// Microchip libraries
#include <xc.h>
#include <sys/attribs.h>

#include "Leds.h"
#include "Ascii.h"
#include "Buttons.h"
#include "Oled.h"
#include "Adc.h"


// **** Set any macros or preprocessor directives here ****
#define SECOND 5
#define BAKE_DEFAULT_TEMP 350
#define BAKE_START_TEMP 300
#define BROIL_START_TEMP 500


// **** Set any local typedefs here ****

typedef enum { // Oven State Machine variables
    SETUP, SELECTOR_CHANGE_PENDING, COOKING, RESET_PENDING
} OvenState;

typedef enum { // Cook Modes
    BAKE, TOAST, BROIL
} CookMode;

typedef enum { // Selector Settings for Bake
    TIME, TEMP
} SelectorSetting;

typedef struct {
    OvenState state; // current state of FSM
    //add more members to this struct
    uint16_t temperature; // current temp of toaster
    CookMode cooking_mode; // current cook mode 
    int cooking_start_time; // cook start time
    uint16_t cooking_time_left; // time left to cook
    uint16_t button_press_time; // stores initial button press down time
    SelectorSetting input_selector; // current input selector
} OvenData;

typedef struct { 
    uint8_t event; // general event flag
    uint16_t freeRunningCounter; // free counter
    uint8_t TIMER_TICK; // timer tick flag
} Timer;

typedef struct {
    uint8_t changed; // ADC event flag
    uint16_t voltage; // current pot voltage
} Adc;

typedef struct {
    uint8_t button_val; // current button state

} Button_Data;

// **** Declare any datatypes here ****

// **** Define any module-level, global, or external variables here ****
static OvenData ovdat; // initialize structs
static Timer timer;
static Adc adc;
static Button_Data button_data;

static uint8_t tick_count = 0;

static char print_l1[100]; // initialize strings for sprintf()
static char print_l2[100];
static char print_l3[100];
static char print_l4[100];
static char print_l[100];

static char time_selector = ' '; // strings for printing > arrow
static char temp_selector = ' ';
static char mode[10]; // cooking mode print variable

static char graphic_top[100];
static char graphic_bottom[100];
// **** Put any helper functions here ****

/*This function will update your OLED to reflect the state .*/
void updateOvenOLED(OvenData ovenData)
{
    //update OLED here
    OledClear(OLED_COLOR_BLACK); // clear OLED screen before updating
    if (ovenData.cooking_mode == BAKE) {
        if (ovenData.input_selector == TIME) { // change input selector on screen
            time_selector = '>';
            temp_selector = ' ';
        } else if (ovenData.input_selector == TEMP) {
            time_selector = ' ';
            temp_selector = '>';
        }
    } else {
        time_selector = ' ';
        temp_selector = ' ';
    }

    if (ovdat.state == COOKING) {
        if (ovenData.cooking_mode == BAKE) { // generate oven graphic depending on cook mode
            sprintf(graphic_top, "%s%s%s%s%s", OVEN_TOP_ON, OVEN_TOP_ON, OVEN_TOP_ON, OVEN_TOP_ON, OVEN_TOP_ON);
            sprintf(graphic_bottom, "%s%s%s%s%s", OVEN_BOTTOM_ON, OVEN_BOTTOM_ON, OVEN_BOTTOM_ON, OVEN_BOTTOM_ON, OVEN_BOTTOM_ON);
        } else if (ovenData.cooking_mode == BROIL) {
            sprintf(graphic_top, "%s%s%s%s%s", OVEN_TOP_ON, OVEN_TOP_ON, OVEN_TOP_ON, OVEN_TOP_ON, OVEN_TOP_ON);
            sprintf(graphic_bottom, "%s%s%s%s%s", OVEN_BOTTOM_OFF, OVEN_BOTTOM_OFF, OVEN_BOTTOM_OFF, OVEN_BOTTOM_OFF, OVEN_BOTTOM_OFF);
        } else if (ovenData.cooking_mode == TOAST) {
            sprintf(graphic_top, "%s%s%s%s%s", OVEN_TOP_OFF, OVEN_TOP_OFF, OVEN_TOP_OFF, OVEN_TOP_OFF, OVEN_TOP_OFF);
            sprintf(graphic_bottom, "%s%s%s%s%s", OVEN_BOTTOM_ON, OVEN_BOTTOM_ON, OVEN_BOTTOM_ON, OVEN_BOTTOM_ON, OVEN_BOTTOM_ON);
        }
    } else {
        sprintf(graphic_top, "%s%s%s%s%s", OVEN_TOP_OFF, OVEN_TOP_OFF, OVEN_TOP_OFF, OVEN_TOP_OFF, OVEN_TOP_OFF);
        sprintf(graphic_bottom, "%s%s%s%s%s", OVEN_BOTTOM_OFF, OVEN_BOTTOM_OFF, OVEN_BOTTOM_OFF, OVEN_BOTTOM_OFF, OVEN_BOTTOM_OFF);
    }

    if (ovenData.cooking_mode == BAKE) { // display appropriate cook mode on OLED
        sprintf(mode, "Bake");
    } else if (ovenData.cooking_mode == BROIL) {
        sprintf(mode, "Broil");
    } else if (ovenData.cooking_mode == TOAST) {
        sprintf(mode, "Toast");
    }

    sprintf(print_l1, "|%s|  Mode: %s\n", graphic_top, mode);
    if ((ovenData.cooking_time_left % 60) < 10) { // assign formatted display to strings by line
        sprintf(print_l2, "|     | %cTime: %d:0%d\n", time_selector, ovenData.cooking_time_left / 60, ovenData.cooking_time_left % 60);
    } else {
        sprintf(print_l2, "|     | %cTime: %d:%d\n", time_selector, ovenData.cooking_time_left / 60, ovenData.cooking_time_left % 60);
    }
    sprintf(print_l3, "|-----| %cTemp: %d%sF\n", temp_selector, ovenData.temperature, DEGREE_SYMBOL);
    sprintf(print_l4, "|%s|", graphic_bottom);

    if (ovenData.cooking_mode == BAKE) { // print appropriate cook mode
        sprintf(print_l, "%s%s%s%s", print_l1, print_l2, print_l3, print_l4);
    } else if (ovenData.cooking_mode == BROIL) {
        sprintf(print_l, "%s%s%s%s", print_l1, print_l2, print_l3, print_l4);
    } else {
        sprintf(print_l, "%s%s|-----|\n%s", print_l1, print_l2, print_l4);
    }

    OledDrawString(print_l);
    OledUpdate();
}

/*This function will execute your state machine.  
 * It should ONLY run if an event flag has been set.*/
void runOvenSM(void)
{
    //write your SM logic here.
    switch (ovdat.state) {
    case SETUP:
        if (adc.changed) { // check ADC change event flag
            if (ovdat.cooking_mode == BAKE) {
                if (ovdat.input_selector == TIME) {
                    ovdat.cooking_time_left = (adc.voltage >> 2) + 1; // change cook time
                    ovdat.cooking_start_time = ovdat.cooking_time_left; // change start time
                } else {
                    ovdat.temperature = (adc.voltage >> 2) + BAKE_START_TEMP; // change temp
                }
            } else if (ovdat.cooking_mode == TOAST) {
                ovdat.cooking_time_left = (adc.voltage >> 2) + 1;
                ovdat.cooking_start_time = ovdat.cooking_time_left;
            } else {
                ovdat.cooking_time_left = (adc.voltage >> 2) + 1;
                ovdat.cooking_start_time = ovdat.cooking_time_left;
            }
            updateOvenOLED(ovdat); // update OLED
        }
        if (button_data.button_val & BUTTON_EVENT_3DOWN) {
            ovdat.state = SELECTOR_CHANGE_PENDING; // change to selector change estate
        }
        if (button_data.button_val & BUTTON_EVENT_4DOWN) {
            ovdat.state = COOKING; // change to cooking state
            updateOvenOLED(ovdat);
        }
        break;

    case SELECTOR_CHANGE_PENDING:
        if (button_data.button_val & BUTTON_EVENT_3UP) {
            if (timer.freeRunningCounter - ovdat.button_press_time < SECOND) {
                if (ovdat.cooking_mode == BROIL) {
                    ovdat.cooking_mode = BAKE;
                    ovdat.temperature = BAKE_DEFAULT_TEMP;
                } else if (ovdat.cooking_mode == BAKE) {
                    ovdat.cooking_mode = TOAST;
                } else {
                    ovdat.cooking_mode = BROIL;
                    ovdat.temperature = BROIL_START_TEMP;
                }
                updateOvenOLED(ovdat);
                ovdat.state = SETUP;
            } else {
                if (ovdat.cooking_mode == BAKE) {
                    if (ovdat.input_selector == TEMP) {
                        ovdat.input_selector = TIME;
                    } else {
                        ovdat.input_selector = TEMP;
                    }
                }
                updateOvenOLED(ovdat);
                ovdat.state = SETUP;
            }
        }
        break;

    case COOKING:
        if (button_data.button_val & BUTTON_EVENT_4DOWN) {
            ovdat.state = RESET_PENDING;
        }
        if (timer.TIMER_TICK) {
            if (ovdat.cooking_time_left > 0) {
                if (tick_count == SECOND) { // check for second to pass
                    if (ovdat.cooking_time_left == ovdat.cooking_start_time) { // sets LEDS to all on
                        LEDS_SET(0xFF);
                    }
                    // decrements LEDS based on time
                    if (ovdat.cooking_time_left == (ovdat.cooking_start_time * 7) / 8) { 
                        LEDS_SET(LEDS_GET() << 1);
                    } else if (ovdat.cooking_time_left == (ovdat.cooking_start_time * 6) / 8) {
                        LEDS_SET(LEDS_GET() << 1);
                    } else if (ovdat.cooking_time_left == (ovdat.cooking_start_time * 5) / 8) {
                        LEDS_SET(LEDS_GET() << 1);
                    } else if (ovdat.cooking_time_left == (ovdat.cooking_start_time * 4) / 8) {
                        LEDS_SET(LEDS_GET() << 1);
                    } else if (ovdat.cooking_time_left == (ovdat.cooking_start_time * 3) / 8) {
                        LEDS_SET(LEDS_GET() << 1);
                    } else if (ovdat.cooking_time_left == (ovdat.cooking_start_time * 2) / 8) {
                        LEDS_SET(LEDS_GET() << 1);
                    } else if (ovdat.cooking_time_left == (ovdat.cooking_start_time * 1) / 8) {
                        LEDS_SET(LEDS_GET() << 1);
                    }
                    ovdat.cooking_time_left--; // decrement time left every second
                    updateOvenOLED(ovdat);
                }
            } else {
                LEDS_SET(0x00); // resets LEDS to off
                ovdat.cooking_time_left = ovdat.cooking_start_time; // reset cook time
                ovdat.state = SETUP; // change state to setup
                updateOvenOLED(ovdat);
            }
        }
        break;

    case RESET_PENDING:
        if (button_data.button_val & BUTTON_EVENT_4UP) {
            if (timer.freeRunningCounter - ovdat.button_press_time < SECOND) {
                ovdat.state = COOKING; // change state to cooking if button pressed for < 1s
            } else {
                ovdat.state = SETUP; // change state to setup if pressed for > 1s
                LEDS_SET(0x00); // reset LEDs
                ovdat.cooking_time_left = ovdat.cooking_start_time; // reset cook time
                updateOvenOLED(ovdat);
            }
        }
        break;
    }

}

int main()
{
    BOARD_Init();

    //initalize timers and timer ISRs:
    // <editor-fold defaultstate="collapsed" desc="TIMER SETUP">

    // Configure Timer 2 using PBCLK as input. We configure it using a 1:16 prescalar, so each timer
    // tick is actually at F_PB / 16 Hz, so setting PR2 to F_PB / 16 / 100 yields a .01s timer.

    T2CON = 0; // everything should be off
    T2CONbits.TCKPS = 0b100; // 1:16 prescaler
    PR2 = BOARD_GetPBClock() / 16 / 100; // interrupt at .5s intervals
    T2CONbits.ON = 1; // turn the timer on

    // Set up the timer interrupt with a priority of 4.
    IFS0bits.T2IF = 0; //clear the interrupt flag before configuring
    IPC2bits.T2IP = 4; // priority of  4
    IPC2bits.T2IS = 0; // subpriority of 0 arbitrarily 
    IEC0bits.T2IE = 1; // turn the interrupt on

    // Configure Timer 3 using PBCLK as input. We configure it using a 1:256 prescaler, so each timer
    // tick is actually at F_PB / 256 Hz, so setting PR3 to F_PB / 256 / 5 yields a .2s timer.

    T3CON = 0; // everything should be off
    T3CONbits.TCKPS = 0b111; // 1:256 prescaler
    PR3 = BOARD_GetPBClock() / 256 / 5; // interrupt at .5s intervals
    T3CONbits.ON = 1; // turn the timer on

    // Set up the timer interrupt with a priority of 4.
    IFS0bits.T3IF = 0; //clear the interrupt flag before configuring
    IPC3bits.T3IP = 4; // priority of  4
    IPC3bits.T3IS = 0; // subpriority of 0 arbitrarily 
    IEC0bits.T3IE = 1; // turn the interrupt on;

    // </editor-fold>

    printf("Welcome to gtaustin's Lab07 (Toaster Oven).  Compiled on %s %s.\n", __TIME__, __DATE__);

    //initialize state machine (and anything else you need to init) here
    LEDS_INIT();
    AdcInit();
    ButtonsInit();
    OledInit();

    ovdat.state = SETUP; // initialize variables
    ovdat.cooking_mode = BAKE;
    ovdat.temperature = BAKE_DEFAULT_TEMP;
    ovdat.cooking_start_time = 1;
    ovdat.cooking_time_left = 1;
    ovdat.button_press_time = 0;
    ovdat.input_selector = TIME;

    timer.event = 0;
    timer.freeRunningCounter = 0;
    timer.TIMER_TICK = 0;

    adc.changed = 0;
    adc.voltage = 0;

    button_data.button_val = 0x00;

    updateOvenOLED(ovdat);

    while (1) {
        // Add main loop code here:
        // check for events
        // on event, run runOvenSM()
        // clear event flags
        if (timer.event) { // check for event flags and run oven sm
            runOvenSM();
            timer.event = 0;
        }
        if (timer.TIMER_TICK) {
            runOvenSM();
            timer.TIMER_TICK = 0;
        }
        if (adc.changed) {
            runOvenSM();
            adc.changed = 0;
        }
    }
}

/*The 5hz timer is used to update the free-running timer and to generate TIMER_TICK events*/
void __ISR(_TIMER_3_VECTOR, ipl4auto) TimerInterrupt5Hz(void)
{
    // Clear the interrupt flag.
    IFS0CLR = 1 << 12;

    timer.freeRunningCounter++; // iterate free running counter
    timer.TIMER_TICK = 1; // generate a timer tick event every 5 Hz
    if (tick_count == SECOND) { 
        tick_count = 0; // tick count = 5 means a second has passed
    }
    tick_count++; // tick count iterates at 5 Hz


    //add event-checking code here
}

/*The 100hz timer is used to check for button and ADC events*/
void __ISR(_TIMER_2_VECTOR, ipl4auto) TimerInterrupt100Hz(void)
{
    // Clear the interrupt flag.
    IFS0CLR = 1 << 8;

    button_data.button_val = ButtonsCheckEvents(); // store button events
    if (button_data.button_val != BUTTON_EVENT_NONE) {
        if (button_data.button_val & BUTTON_EVENT_4DOWN || button_data.button_val & BUTTON_EVENT_3DOWN) {
            ovdat.button_press_time = timer.freeRunningCounter; // record button 3/4 press time
        }
        timer.event = 1; // generate event upon button event
    }

    adc.changed = AdcChanged(); // check for ADC change
    if (adc.changed) {
        adc.voltage = AdcRead(); // store ADC voltage
        timer.event = 1; // generate event upon ADC change
    }

    //add event-checking code here
}