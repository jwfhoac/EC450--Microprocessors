/***********************************************************************
	Jenny Hoac
	October 18, 2013
	
	The Timer A interrupt is used to play a full scale of notes using
	combinations of 4 push buttons.
	
 ***********************************************************************/
 
asm(" .length 10000");
asm(" .width 132");
#include "msp430g2553.h"
//-----------------------
// The following definitions allow us to adjust some of the port properties
// for the example:

// define the bit mask (within P1) corresponding to output TA0
#define TA0_BIT 0x02

// define the location for the button (this is the built in button)
// specific bit for the button
#define button1 0x08
#define	button2 0x04
#define button3 0x10
#define button4 0x20

// flags for whether each button is pressed
int button1pressed = 0;
int button2pressed = 0;
int button3pressed = 0;
int button4pressed = 0;

#define initialHalfPeriod 500
// notes to play
// note: the values are the period in microseconds
// which is the period obtains by each note's frequency in Hz times the half period times 1000
// (1/freq)*initialHalfPeriod*1000
#define F4 2900
#define G4 2550
#define A4 2270
#define B4b 2100
#define C5 1900
#define D5 1700
#define E5 1500
#define F5 1400

//----------------------------------
void init_timer(void); // routine to setup the timer
void init_button(void); // routine to setup the button

// ++++++++++++++++++++++++++
void main(){
	WDTCTL = WDTPW + WDTHOLD;	// Stop watchdog timer
	BCSCTL1 = CALBC1_8MHZ;    // 8Mhz calibration for clock
	DCOCTL  = CALDCO_8MHZ;

	init_timer();  // initialize timer
	init_button(); // initialize button press
	_bis_SR_register(GIE+LPM0_bits);// enable general interrupts and power down CPU
}


// +++++++++++++++++++++++++++
// Sound Production System
void init_timer(){              // initialization and start of timer
	TA0CTL |= TACLR;              // reset clock
	TA0CTL = TASSEL_2+ID_3+MC_1;  // clock source = SMCLK
	                            // clock divider=8
	                            // UP mode
	                            // timer A interrupt off
	TA0CCTL0=0; // compare mode, output 0, no interrupt enabled
	TA0CCR0 = initialHalfPeriod-1; // in up mode TAR=0... TACCRO-1
	P1SEL|=TA0_BIT; // connect timer output to pin
	P1DIR|=TA0_BIT;
}

// +++++++++++++++++++++++++++
// button input System
// button toggles the state of the sound (on or off)
// action will be interrupt driven on a downgoing signal on the pin
// no debouncing (to see how this goes)

void init_button(){
// All GPIO's are already inputs if we are coming in after a reset
	P1OUT |= button1 + button2 + button3 + button4; // pullup
	P1REN |= button1 + button2 + button3 + button4; // enable resistor
	P1IES |= button1 + button2 + button3 + button4; // set for 1->0 transition
	P1IFG &= ~(button1 + button2 + button3 + button4);// clear interrupt flag
	P1IE  |= button1 + button2 + button3 + button4; // enable interrupt
}

void interrupt buttonhandler(){
// check that this is the correct interrupt
// (if not, it is an error, but there is no error handler)

	if(P1IFG & button1){	// interrupt called for change in button 1
		// if the button was pressed (a 1->0 transition), turn on flag
		if(P1IES & button1)
		{
			button1pressed = 1;
		}
		// if button let go (0->1 transition), turn off flag
		else if(P1IES & ~button1)
		{
			button1pressed = 0;
		}
		P1IFG &= ~button1; // reset the interrupt flag
		P1IES ^= button1; // toggles between looking for a 1->0 transition and 0->1 transition
	}
	else if(P1IFG & button2){	// interrupt called for change in button 2
		// if the button was pressed (a 1->0 transition), turn on flag
		if(P1IES & button2)
		{
			button2pressed = 1;
		}
		// if button let go (0->1 transition), turn off flag
		else if(P1IES & ~button2)
		{
			button2pressed = 0;
		}
		P1IFG &= ~button2; // reset the interrupt flag
		P1IES ^= button2; // toggles between looking for a 1->0 transition and 0->1 transition
	}
	else if(P1IFG & button3){	// interrupt called for change in button 3
		// if the button was pressed (a 1->0 transition), turn on flag
		if(P1IES & button3)
		{
			button3pressed = 1;
		}
		// if button let go (0->1 transition), turn off flag
		else if(P1IES & ~button3)
		{
			button3pressed = 0;
		}
		P1IFG &= ~button3; // reset the interrupt flag
		P1IES ^= button3; // toggles between looking for a 1->0 transition and 0->1 transition
	}
	else if(P1IFG & button4){	// interrupt called for change in button 4
		// if the button was pressed (a 1->0 transition), turn on flag
		if(P1IES & button4)
		{
			button4pressed = 1;
		}
		// if button let go (0->1 transition), turn off flag
		else if(P1IES & ~button4)
		{
			button4pressed = 0;
		}
		P1IFG &= ~button4; // reset the interrupt flag
		P1IES ^= button4; // toggles between looking for a 1->0 transition and 0->1 transition
	}

	if(button1pressed == 1)
	{
		TACCTL0 |= OUTMOD_4;	// want out mod 4 when any buttons are pressed
		if(button2pressed == 1)
		{
			TA0CCR0 = C5;	// fifth note, middle C, played if both buttons 1 and 2 pressed
		}
		else if(button4pressed == 1)
		{
			TA0CCR0 = F5;	// last (eighth) F note in scale played if both buttons 1 and 4 pressed
		}
		else
		{
			TA0CCR0 = F4;	// first F note in scale played when button 1 pressed
		}
	}
	else if(button2pressed == 1)
	{
		TACCTL0 |= OUTMOD_4;	// want out mod 4 when any buttons are pressed
		if(button3pressed == 1)
		{
			TA0CCR0 = D5;	// sixth note, D in octave 5, played when buttons 2 and 3 pressed
		}
		else
		{
			TA0CCR0 = G4;	// second note, G, played when button 2 pressed
		}
	}
	else if(button3pressed == 1)
	{
		TACCTL0 |= OUTMOD_4;	// want out mod 4 when any buttons are pressed
		if(button4pressed == 1)
		{
			TA0CCR0 = E5;	// seventh note, E in octave 5, played when buttons 3 and 4 pressed
		}
		else
		{
			TA0CCR0 = A4;	// third note, A, played when button 3 pressed
		}
	}
	else if(button4pressed == 1)
	{
		TACCTL0 |= OUTMOD_4;	// want out mod 4 when any buttons are pressed
		TA0CCR0 = B4b;	// fourth note, B flat, played when button 4 pressed
	}
	else	// if no buttons are currently pressed, reset
	{
		TA0CCR0 = initialHalfPeriod;
		TACCTL0 ^= OUTMOD_4;	// toggle off out mod 4
	}
}

ISR_VECTOR(buttonhandler,".int02") // declare interrupt vector
// +++++++++++++++++++++++++++
