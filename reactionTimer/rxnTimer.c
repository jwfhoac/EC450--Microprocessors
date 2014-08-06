/***********************************************************************
	Jenny Hoac
	October 23, 2013
	
	This program is a simple reaction timer. 
	Upon the press of a button, an LED will turn on signaling the user
	to press a separate button. 
	The reaction time (ie. the time between the LED turns on and
	the reaction triggered button is pressed) 
	is then calculated and stored.
	
 ***********************************************************************/


#include "msp430g2553.h"
#include "stdlib.h"

// Definitions of hardware, TA1 on port P1.2
#define RSTBUTTON 0x08
#define BUTTON 0x04
#define RED 0x01

// Headers for initialization functions
void init_timer(void);
void init_button(void);

// global variables
// -- for TA: counting reaction time
unsigned int overflows;	   	// counter for the number of overflows
unsigned long startTime;	// records when LED turns on
unsigned long endTime;		// records when user reacts to LED on
unsigned long rawEndTime;	// to check reaction time
unsigned long rxnTime;

// -- for WDT: delay
unsigned int startDelay;	// flag for when to begin counting delay
unsigned int randSeed;		// stores a random number
unsigned int delayInterval;	// stores length of time before LED turns on after reset button pressed
unsigned int delayCounter;

void main(){
	// setup the watchdog timer as an interval timer
	WDTCTL =(WDTPW + 	// (bits 15-8) password
						// bit 7=0 => watchdog timer on
						// bit 6=0 => NMI on rising edge (not used here)
						// bit 5=0 => RST/NMI pin does a reset (not used here)
			WDTTMSEL + 	// (bit 4) select interval timer mode
			WDTCNTCL +  // (bit 3) clear watchdog timer counter
			0 + 		// bit 2=0 => SMCLK is the source
			1); 		// bits 1-0 = 01 => source/8K

	IE1 |= WDTIE;		// enable the WDT interrupt (in the system interrupt register IE1)

	BCSCTL1 = CALBC1_8MHZ; // 8Mhz calibration for clock
	DCOCTL = CALDCO_8MHZ;

	// initialize the WDT variables
	startDelay = 0;
	delayCounter = 0;
	delayInterval = 1;

	init_timer(); // initialize timer
	init_button(); // initialize buttons
	_bis_SR_register(GIE+LPM0_bits); // enable CPU interrupts and power off CPU
}

void init_timer(){ // initialization and start of timer
	TACTL |=TACLR; // reset clock
	TACTL =TASSEL_2+ID_3+MC_2+TAIE; // clock source = SMCLK, enable overflow interrupt
									// clock divider=8
									// continuous mode
									// timer A interrupt on for overflows
	TA0CCTL1=CM_2+CAP+CCIE; // capture mode - falling edge on CCI1A, enable interrupt 1
}

void init_button(){
	P1SEL |= BUTTON; 	// connect timer to pin
	P1DIR &= ~BUTTON; 	// set P1.3 as input (button)
	P1DIR |= RED;		// set P1.0 as output (LED)
	P1OUT |= (BUTTON+RSTBUTTON); // enable pullup
	P1OUT &= ~RED;		// turn off LED
	P1REN |= BUTTON+RSTBUTTON; 	// enable internal 'PULL' resistor for the button
}

// +++++++++++++++++++++++++++
void interrupt TA_handler(){
	// this handler is called for either channel 1 (TAIV==2) or overflow (TAIV==10)
	switch (TAIV) { // read highest priority interrupt and clear flag
		case 2: { // interrupt called for TA1 channel interrupt
			P1OUT &= ~RED;
			if (TACCTL1 & CAP) {// are we in input capture mode?
				// if we are in capture mode, save the full time

				union a { // a union to let us deal with a long word as 2 ints
					unsigned long L;
					unsigned int words[2];
				} capture_time;

				// save the time
				capture_time.words[0] = TAR; 		// 16 bit capture time
				rawEndTime = capture_time.words[0];	// for checking the value of rxnTime
				capture_time.words[1] = overflows; 	// high bits from overflows
				endTime = capture_time.L;			// combine and store as end time

				rxnTime = endTime - startTime;		// calculate reaction time given end time (just captured) and start time
			}
			else {
				// must be in compare mode at end of the time interval
				TACCTL1 |= CAP; // go back to capture mode
			}

		}
		break;
		case 10: { // interrupt called for overflow
			++overflows;
		}
	}
}

ISR_VECTOR(TA_handler,".int08")

// +++++++++++++++++++++++++++
// Watchdog Timer Interrupt Handler -- is called regularly at intervals of 8k/8Mhz = 1ms
void interrupt WDT_interval_handler(){
	if((~P1IN & RSTBUTTON)&&(delayCounter == 0)){	// if button pressed and not counting delays
		randSeed = rand() % 100;				// generate random number
		delayInterval = (randSeed*40) + 2000;	// transform into desire range of numbers
												// 		for random delay interval  (of ~2-6 seconds)

		startDelay = 1;		// flag goes off to start counting delays
	}

	if(startDelay == 1){ // in delay mode
		delayCounter++;
	}

	if(delayCounter == delayInterval){	// once delay counter reaches value of interval
		startDelay = 0;		// turn delay flag off -> out of delay mode
		delayCounter = 0;	// reset delay counter

		overflows = 0;		// reset overflows
		endTime = 0;		// reset end time
		P1OUT |= RED;		// turn LED on
		startTime = TAR;	// record current time (to time next reaction)
	}

}

ISR_VECTOR(WDT_interval_handler,".int10")
