/*
 * File:   main.c
 * Author: Dewayne
 *
 * Created on December 19, 2020, 9:33 PM.
 * Version  Date         Who      Description
 * -------  -----------  -------  ------------------------------------------------------------------
 * 1.0      2020-12-19   Dewayne  Initial creation
 * 2.0      2024-06-19   Dewayne  Added software PWM for better candle effect and restructured the 
 *                                code for better readability and understanding.
 */
#include <xc.h>
#include <stdlib.h>
#include <math.h>

// PIC12F683 Configuration Bit Settings

// CONFIG
#pragma config FOSC = INTOSCIO  // Internal osc with pins set to I/O
#pragma config WDTE = OFF       // Watchdog Timer Enable bit (WDT disabled)
#pragma config PWRTE = ON       // Power-up Timer Enable bit (PWRT enabled)
#pragma config MCLRE = ON       // MCLR is enabled
#pragma config CP = OFF         // Code protection is disabled)
#pragma config CPD = OFF        // Data protection is disabled)
#pragma config BOREN = ON       // Brown Out Detect (BOR enabled)
#pragma config IESO = OFF       // Internal/External Switchover mode disabled
#pragma config FCMEN = OFF      // Fail-Safe Clock Monitor is disabled

#define _XTAL_FREQ 8000000      // Clock frequency is 8mHz
#define MAX_LEVEL 32            // PWM brightness steps
#define FRAMES_PER_STEP 4       // PWM frames between brightness steps (~6 mS)

/*
 * Global state values
 */
struct {
    unsigned triggered : 1; // We are triggered 
    unsigned unused : 7; // Unused
} flags;

/**
 * Forward definitions
 */
void initialize(void);
unsigned char approach(unsigned char level, unsigned char target);

/**
 * Interrupt Service Routine.
 *
 * This routine handles the comparator interrupt and updates the triggered flag accordingly.
 * The comparator is used to detect changes in light levels via a photocell.  If a photocell
 * is not installed, the comparator will trigger immediately and the system is controlled 
 * directly by being powered on/off from whatever source is controlling it.
 * 
 * If a CDS (Cadmium Sulfide) photocell is used, it will change resistance based on light levels, 
 * causing the comparator to trigger when the ambient light falls below a certain threshold.
 * This allows the system to automatically respond to changes in ambient light without manual 
 * intervention, providing on at dusk and off at dawn functionality automatically.
 */
void __interrupt() isr(void) {

    /*
     * Check if it was a comparator interrupt and handle if it was.  Set the triggered
     * flag based on the comparator output.
     */
    if (PIR1bits.CMIF) {
        flags.triggered = CMCON0bits.COUT; // If comparator is 1, we are trig
        PIR1bits.CMIF = 0; // reset interrupt flag
    }
}

/**
 * Mainline
 */
int main(void) {
    initialize();

    unsigned char red = 0, yellow1 = 0, yellow2 = 0;
    unsigned char redTarget = 0, yellow1Target = 0, yellow2Target = MAX_LEVEL / 2;
    unsigned char frames = 0;

    /*
     * Initially check if the comparator is triggered because if not, we need to 
     * just wait for the comparator to trigger.
     */
    if (CMCON0bits.COUT) {
        flags.triggered = 1;
    }

loop:

    /**
     * If we're not triggered, then sleep.  If the comparator output trips because of 
     * a photocell detecting dark, then  we will be interrupted and set as triggered, 
     * which will bypass this check.
     */
    if (!flags.triggered) {
        GPIObits.GP2 = 0;
        GPIObits.GP4 = 0;
        GPIObits.GP5 = 0; 
        SLEEP();
    }

    /*
     * Software PWM: 
     *
     * This loop generates a PWM signal for each LED string based on a 40uS period,
     * which results in a PWM frequency of approximately 650 Hz.  The duty cycle for 
     * each LED string is determined by its current brightness level relative to 
     * MAX_LEVEL.  The current brightness levels are gradually adjusted toward their 
     * targets to create a smooth fading effect.
     */
    for (unsigned char slot = 0; slot < MAX_LEVEL; slot++) {
        GPIObits.GP2 = (red > slot);
        GPIObits.GP4 = (yellow1 > slot);
        GPIObits.GP5 = (yellow2 > slot);
        __delay_us(40);
    }

    /*
     * Update the brightness levels for each LED string if enough frames have passed.
     */
    if (++frames >= FRAMES_PER_STEP) {
        frames = 0;
        red = approach(red, redTarget);
        yellow1 = approach(yellow1, yellow1Target);
        yellow2 = approach(yellow2, yellow2Target);

        /*
         * When a string reaches its target, pick a new one.  Yellow 2 stays 
         * within a narrower, brighter range so the candle never goes dark.
         */
        if (red == redTarget) {
            redTarget = rand() % (MAX_LEVEL + 1);
        }
        if (yellow1 == yellow1Target) {
            yellow1Target = rand() % (MAX_LEVEL + 1);
        }
        if (yellow2 == yellow2Target) {
            yellow2Target = (MAX_LEVEL / 3) + rand() % (MAX_LEVEL / 3);
        }
    }

    goto loop;
    return 0;   // Make the compiler happy...
}

/**
 * Move a level one step toward its target, either increasing or decreasing it as needed.
 */
unsigned char approach(unsigned char level, unsigned char target) {
    if (level < target) {
        return level + 1;
    }
    if (level > target) {
        return level - 1;
    }
    return level;
}

/**
 * Build a random seed from ADC noise on GP0/AN0.
 *
 * GP0 is only used for programming/debugging, so it floats in operation and the
 * least significant bit of each conversion is essentially noise that differs
 * from unit to unit.  The ADC is turned off again afterwards.  we use this
 * noise to seed the random number generator so that each unit has a unique seed.
 */
static unsigned int noiseSeed(void) {
    unsigned int seed = 0;
    unsigned char savedAnsel = ANSEL;

    TRISIObits.TRISIO0 = 1; // GP0 is input
    ANSEL = 0x51; // AN0 analog, Fosc/16 conversion clock (ADCS is in ANSEL on this part)
    ADCON0 = 0x01; // Right justified, AN0, ADC on
    for (unsigned char i = 0; i < 16; i++) {
        __delay_us(20); // Acquisition time
        ADCON0bits.GO = 1;
        while (ADCON0bits.GO);
        seed = (seed << 1) | (ADRESL & 1);
    }
    ADCON0 = 0x00; // ADC off
    ANSEL = savedAnsel; // Restore analog configuration
    return seed;
}

/**
 * Initialize the processor. 
 *
 * This includes setting up GPIO, comparator, voltage reference, and random number generator.
 */
void initialize(void) {
    /*
     * Initially turn off all interrupts and sources, then enable just the 
     * comparator as a source, but leave global interrupt flag clear until
     * we've finished the initialization
     */
    INTCON = 0x00; // Disable all interrupts 
    PIE1 = 0x00; // And all peripheral interrupt sources
    IOC = 0x00; // And all interrupt on pin change sources

    /*
     * Clear all state 
     */
    flags.triggered = 0; // Turn off all saved state 
    GPIObits.GP2 = 0;
    GPIObits.GP4 = 0;
    GPIObits.GP5 = 0;

    /*
     * Now lets set up the GPIO control.  Set everything as digital inputs 
     * initially, then reset GP2, 4, and 5 as outputs, and GP1 as analog 
     * input to the comparator. 
     */
    IOC = 0x00; // no interrupt on change
    TRISIO = 0x3F; // All pins initially input 
    ANSEL = 0;
    ANSELbits.ANS1 = 1; // AN1 is analog
    WPU = 0x00; // Turn off all weak pull ups
    TRISIObits.TRISIO2 = 0; // GP2 is output (RED)
    TRISIObits.TRISIO4 = 0; // GP4 is output (YELLOW 1)
    TRISIObits.TRISIO5 = 0; // GP5 is output (YELLOW 2)

    /*
     * We don't use the ADC module 
     */
    ADCON0 = 0x00;

    /*
     * Set up the comparator and the internal voltage reference
     */
    VRCONbits.VRR = 1; // Low range
    VRCONbits.VR = 0b1100; // 12/24 * Vdd = 1.65V
    VRCONbits.VREN = 1; // Enable voltage reference
    // Inv input is AN1, Non-inv input is VRef, 
    // CM out is internal
    CMCON0bits.CM2 = 1;
    CMCON0bits.CM1 = 0;
    CMCON0bits.CM0 = 0;
    CMCON0bits.CIS = 0; // Source is AN1 
    CMCON0bits.CINV = 0; // non-inverted

    /*
     * Initialize (seed) the random number generator from ADC noise so that each
     * unit on a shared bus produces a different sequence.
     */
    srand(noiseSeed());

    /*
     * Now that we have everything set up, lets enable the global interrupts 
     */
    PIR1 = 0; // Clear any pending interrupts because of config changes
    INTCONbits.PEIE = 1; // Then enable peripheral interrupts
    PIE1bits.CMIE = 1; // And enable comparator interrupt 
    INTCONbits.GIE = 1; // Global interrupts enabled
}
