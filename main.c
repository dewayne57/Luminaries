/*
 * File:   main.c
 * Author: Dewayne
 * Description: Main program for controlling a candle-like LED effect using a
 * PIC12F683 microcontroller. The program uses a software PWM to control the
 * brightness of red and yellow LEDs, and a comparator to detect ambient light
 * changes via a photocell. The software PWM allows for smooth transitions in
 * LED brightness, simulating the flickering effect of a candle.
 *
 *              This program is intended to run on a PIC12F683 microcontroller
 * with an internal 8 MHz oscillator. The red and yellow LEDs are connected to
 * GP2, GP4, and GP5 pins, and the photocell is connected to the comparator
 * input. The software PWM is implemented in the main loop, and the comparator
 * interrupt is used to detect changes in ambient light.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Created on December 19, 2020, 9:33 PM.
 * Version  Date         Who      Description
 * -------  -----------  -------  --------------------------------------------
 * 1.0      2020-12-19   Dewayne  Initial creation
 * 2.0      2024-06-19   Dewayne  Added software PWM for better candle effect
 *                                and restructured the code for better
 *                                readability and understanding.
 */
#include <stdlib.h>
#include <xc.h>

// PIC12F683 Configuration Bit Settings

// CONFIG
#pragma config FOSC = INTOSCIO // Internal osc with pins set to I/O
#pragma config WDTE = OFF      // Watchdog Timer Enable bit (WDT disabled)
#pragma config PWRTE = ON      // Power-up Timer Enable bit (PWRT enabled)
#pragma config MCLRE = OFF     // MCLR is enabled
#pragma config CP = OFF        // Code protection is disabled)
#pragma config CPD = OFF       // Data protection is disabled)
#pragma config BOREN = ON      // Brown Out Detect (BOR enabled)
#pragma config IESO = OFF      // Internal/External Switchover mode disabled
#pragma config FCMEN = OFF     // Fail-Safe Clock Monitor is disabled

#define _XTAL_FREQ 8000000   // Clock frequency is 8mHz
#define MAX_LEVEL 32         // PWM brightness steps
#define FRAMES_PER_STEP 4    // PWM frames between brightness steps (~6 mS)
#define VDD_VOLTAGE_MV 5000u // Circuit supply voltage in millivolts

// Use two different comparator "trip" points to introduce hysteresis in the
// comparator input to avoid rapid toggling when the input is near the threshold.
// The low trip point is used to determine when the comparator should trigger,
// while the high trip point is used to determine when the comparator should reset.
#define COMPARATOR_LOW_TRIP_MV 300u
#define COMPARATOR_HIGH_TRIP_MV 350u

/*
 * Global state values
 */
struct {
  unsigned triggered : 1; // We are triggered
  unsigned unused : 7;    // Unused
} flags;

/*
 * Forward definitions
 */
void initialize(void);
unsigned char approach(unsigned char level, unsigned char target);
static void setComparatorTripVoltage(unsigned int thresholdMv);

/// Interrupt Service Routine for the comparator.
/// @brief Handles the comparator interrupt and updates the triggered flag.
/// @return None
/// @note This ISR is triggered by the comparator interrupt flag (CMIF) and
/// should be kept as short as possible to ensure timely response to comparator
/// events. This routine handles the comparator interrupt and updates the
/// triggered flag accordingly. The comparator is used to detect changes in
/// light levels via a photocell.  If a photocell is not installed, the
/// comparator will trigger immediately and the system is controlled directly by
/// being powered on/off from whatever source is controlling it.
///
/// If a CDS (Cadmium Sulfide) photocell is used, it will change resistance
/// based on light levels, causing the comparator to trigger when the ambient
/// light falls below a certain threshold. This allows the system to
/// automatically respond to changes in ambient light without manual
/// intervention, providing on at dusk and off at dawn functionality
/// automatically.
void __interrupt() isr(void) {

  /*
   * Check if it was a comparator interrupt and handle if it was.  Set the
   * triggered flag based on the comparator output.
   */
  if (PIR1bits.CMIF) {
    flags.triggered = CMCON0bits.COUT; // If comparator is 1, we are trig
    setComparatorTripVoltage(flags.triggered ? COMPARATOR_HIGH_TRIP_MV
                                             : COMPARATOR_LOW_TRIP_MV);
    PIR1bits.CMIF = 0; // reset interrupt flag
  }
}

/// @brief Main program loop for controlling the LED strings based on comparator
/// input.
/// @return None
/// @details This function initializes the system and enters an infinite loop
/// where it checks the comparator status, sleeps if not triggered, and controls
/// the LED strings using software PWM based on the current and target
/// brightness levels.
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
    setComparatorTripVoltage(COMPARATOR_HIGH_TRIP_MV);
  }

loop:

  /**
   * If we're not triggered, then sleep.  If the comparator output trips because
   * of a photocell detecting dark, then  we will be interrupted and set as
   * triggered, which will bypass this check.
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
   * This loop generates a PWM signal for each LED string based on a 40uS
   * period, which results in a PWM frequency of approximately 650 Hz.  The duty
   * cycle for each LED string is determined by its current brightness level
   * relative to MAX_LEVEL.  The current brightness levels are gradually
   * adjusted toward their targets to create a smooth fading effect.
   */
  for (unsigned char slot = 0; slot < MAX_LEVEL; slot++) {
    GPIObits.GP2 = (red > slot);
    GPIObits.GP4 = (yellow1 > slot);
    GPIObits.GP5 = (yellow2 > slot);
    __delay_us(40);
  }

  /*
   * Update the brightness levels for each LED string if enough frames have
   * passed.
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
      redTarget = (unsigned char)(rand() % (MAX_LEVEL + 1));
    }
    if (yellow1 == yellow1Target) {
      yellow1Target = (unsigned char)(rand() % (MAX_LEVEL + 1));
    }
    if (yellow2 == yellow2Target) {
      yellow2Target =
          (unsigned char)((MAX_LEVEL / 3) + (rand() % (MAX_LEVEL / 3)));
    }
  }

  goto loop;
  return 0; // Make the compiler happy...
}

/// @brief Gradually adjusts a brightness level toward its target.
/// @param level The current brightness level.
/// @param target The target brightness level.
/// @return The new brightness level after moving one step toward the target.
unsigned char approach(unsigned char level, unsigned char target) {
  if (level < target) {
    return level + 1;
  }
  if (level > target) {
    return level - 1;
  }
  return level;
}

/// @brief Programs the comparator voltage-reference threshold.
/// @param thresholdMv Desired voltage reference in millivolts.
static void setComparatorTripVoltage(unsigned int thresholdMv) {
  unsigned int vddMv = VDD_VOLTAGE_MV;
  unsigned char vrCode = 0;
  unsigned int lowRangeMaxMv = (vddMv * 15u) / 24u;
  unsigned int highRangeMaxMv = (vddMv * 23u) / 32u; // 0.71875 * Vdd

  if (thresholdMv > highRangeMaxMv) {
    thresholdMv = highRangeMaxMv;
  }

  if (thresholdMv <= lowRangeMaxMv) {
    VRCONbits.VRR = 1; // Low range: Vref = Vdd * (VR / 24)
    vrCode = (unsigned char)((thresholdMv * 24u + (vddMv / 2u)) / vddMv);
  } else {
    VRCONbits.VRR = 0; // High range: Vref = Vdd * (0.25 + VR/32)
    vrCode =
        (unsigned char)(((thresholdMv * 32u) + (vddMv / 2u)) / vddMv - 8u);
  }

  if (vrCode > 0x0F) {
    vrCode = 0x0F;
  }

  VRCONbits.VR = vrCode;
}

/// @brief Build a random seed from ADC noise on GP0/AN0.
/// @return A 16-bit random seed generated from ADC noise.
/// @details The function reads the least significant bit of 16 consecutive ADC
/// conversions on GP0/AN0, which is essentially random due to the floating
/// nature of the pin during normal operation.  These bits are combined to form
/// a 16-bit seed for the random number generator.
static unsigned int noiseSeed(void) {
  unsigned int seed = 0;
  unsigned char savedAnsel = ANSEL;

  TRISIObits.TRISIO0 = 1; // GP0 is input
  ANSEL = 0x51;  // AN0 analog, Fosc/16 conversion clock (ADCS is in ANSEL on
                 // this part)
  ADCON0 = 0x01; // Right justified, AN0, ADC on
  for (unsigned char i = 0; i < 16; i++) {
    __delay_us(20); // Acquisition time
    ADCON0bits.GO = 1;
    while (ADCON0bits.GO)
      ;
    seed = (seed << 1) | (ADRESL & 1);
  }
  ADCON0 = 0x00;      // ADC off
  ANSEL = savedAnsel; // Restore analog configuration
  return seed;
}

/// @brief Initialize the processor, including GPIO, comparator, voltage
/// reference, and RNG.
/// @return None
/// @details This function configures the GPIO pins, sets up the comparator and
/// voltage reference, and seeds the random number generator using ADC noise.
void initialize(void) {
  /*
   * Initially turn off all interrupts and sources, then enable just the
   * comparator as a source, but leave global interrupt flag clear until
   * we've finished the initialization
   */
  INTCON = 0x00; // Disable all interrupts
  PIE1 = 0x00;   // And all peripheral interrupt sources
  IOC = 0x00;    // And all interrupt on pin change sources

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
  IOC = 0x00;    // no interrupt on change
  TRISIO = 0x3F; // All pins initially input
  ANSEL = 0;
  ANSELbits.ANS1 = 1;     // AN1 is analog
  WPU = 0x00;             // Turn off all weak pull ups
  TRISIObits.TRISIO2 = 0; // GP2 is output (RED)
  TRISIObits.TRISIO4 = 0; // GP4 is output (YELLOW 1)
  TRISIObits.TRISIO5 = 0; // GP5 is output (YELLOW 2)

  /*
   * We don't use the ADC module
   */
  ADCON0 = 0x00;

  /*
   * Start with the low trip point.  Once the comparator triggers, the ISR
   * raises this reference to the high trip point until the input resets.
   */
  setComparatorTripVoltage(COMPARATOR_LOW_TRIP_MV);
  VRCONbits.VREN = 1; // Enable voltage reference
  // Inverting input of the comparator is AN1, Non-inverting input is VRef,
  // and the comparator output is internal
  CMCON0bits.CM2 = 1;
  CMCON0bits.CM1 = 0;
  CMCON0bits.CM0 = 0;
  CMCON0bits.CIS = 0;  // Source is AN1
  CMCON0bits.CINV = 0; // non-inverted

  /*
   * Initialize (seed) the random number generator from ADC noise so that each
   * unit on a shared bus produces a different sequence.
   */
  srand(noiseSeed());

  /*
   * Now that we have everything set up, lets enable the global interrupts
   */
  PIR1 = 0;            // Clear any pending interrupts because of config changes
  INTCONbits.PEIE = 1; // Then enable peripheral interrupts
  PIE1bits.CMIE = 1;   // And enable comparator interrupt
  INTCONbits.GIE = 1;  // Global interrupts enabled
}
