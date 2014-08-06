/***********************************************************************
	Jenny Hoac & Alexander Kithes
	December 11, 2013
	
	This project mimics a capture the flag / seize and secure type of laser game.
	
	Rough Game Logic:
	The game consists of two teams with the goal of seizing a flag and securing it.
	A team seizes the flag when the corresponding target is "shot at" by a laser
	gun for a period of time. A counter is displayed on the LCD during this time.
	A separate timer keeps track of how long the flag is in possession of a team
	and scoring is calculated accordingly.
	
	Peripherals include two "laser gun" circuits, an LCD for score keeping,
	an LED "flag" and two shooting "targets".
	
 ***********************************************************************/

 #include <msp430.h>

#include "msp430g2553.h"
#define	LCDDATA	0x0F	// P2.3-P2.0 correspond directly to D7-D4 on LCD, a nibble's worth of data (O)
#define	LCDRS	0x10	// P1.4, Register Select, used to establish whether it is an instruction or data being sent (O)
#define	LCDRW	0x20	// P1.5, Read/~Write Select (O)
#define	LCDE	0x40	// P1.6, Enable/Enter, normal high and negative edge triggers ADC data collection (O)

#define	REDTAKE	0x01	// P1.0, goes high when red team is taking flag (IR phototransistor is activated) (I)
#define	GRNTAKE	0x02	// P1.1, goes high when green team is taking flag (IR phototransistor is activated) (I)
#define	ENTER	0x04	// P1.2, goes high when user wants to set currently displayed gametime and start game (I)
#define	UP		0x08	// P1.3, goes high when user wants to increment gametime, varying from 2 to 20 (I)
#define	REDFLAG	0x10	// P2.4, turns the red "flag" LEDs on (O)
#define GRNFLAG	0x20	// P2.5, turns the green "flag" LEDs on (O)
#define ONE_SEC 250		// number of WDT cycles until 1 second is reached
#define INIT_CNT 67		// number of WDT cycles for LCD initialization

void init_gpio(void);
void LCD_init(char);
void LCD_LUT(char);
void cmdtoLCD(char, char);
void chartoLCD(char, char);
void gotoLCDpos(char, char, char);

//for using LCD
volatile unsigned char whichcase = 0;	// use in multi-cycle LCD setup/operations, counted by WDT interrupt handler
volatile unsigned char letternibble = 1;	// use to vary between 1st and 2nd nibble
volatile unsigned char letter = 0;	// int used to store 8-bit ASCII code for letter to be sent to LCD
volatile unsigned char highnibble = 0x30;
volatile unsigned char lownibble = 0;	// two global variables that the
volatile unsigned char LCDlocation = 0; // hex number from 0x00 to 0x67 indicating which of 80 positions (where 00-0A and 40-4A are on the LCD)
char begin_update;
char singles_digit;
char tens_digit;

//FSM
char last_state;
char state;
char begin_countdown;
//main timer
char MINUTES;
char SECONDS;
int wdt_ctr_timer;
//for WDT counting
int wdt_ctr;			// counts number of WDT cycles
char second_ctr;			// counts number of seconds
//score keeping
char RED_SCORE;
char GRN_SCORE;
char WINNER;
//when flag is owned
int wdt_ctr_flg;			// counts number of WDT cycles
char second_ctr_flg;			// counts number of seconds
char OWN_SECONDS;
char OWN_MINUTES;
//to check if target just hit or just let go
char REDPRESS;
char GRNPRESS;

int main(void) {
	  // setup the watchdog timer as an interval timer
	WDTCTL =(WDTPW +	// (bits 15-8) password
						// bit 7=0 => watchdog timer on
						// bit 6=0 => NMI on rising edge (not used here)
						// bit 5=0 => RST/NMI pin does a reset (not used here)
				WDTTMSEL +   // (bit 4) select interval timer mode
				WDTCNTCL); 	// (bit 3) clear watchdog timer counter
							// bit 2=0 => SMCLK is the source
				            // bits 1-0 = 10 => source/32K.


	IE1 |= WDTIE;		// enable the WDT interrupt (in the system interrupt register IE1)

	BCSCTL1 = CALBC1_8MHZ;
	DCOCTL = CALDCO_8MHZ;	//8MHz calibration for clock

	begin_update = 0;

	last_state = 0;
	state = 'i';
	begin_countdown = 0;

	MINUTES = 2;
	SECONDS = 0;
	wdt_ctr_timer = 0;

	wdt_ctr = 0;
	second_ctr = 0;

	RED_SCORE = 0;
	GRN_SCORE = 0;

	OWN_SECONDS = 0;
	OWN_MINUTES = 0;

	REDPRESS = 0;
	GRNPRESS = 0;

	init_gpio();

	_bis_SR_register(GIE+LPM0_bits);
}

void init_gpio(){
	P1DIR |= (LCDRS + LCDRW + LCDE);
	P2DIR |= (LCDDATA);
	P2DIR |= (REDFLAG + GRNFLAG);

	P2OUT &= ~REDFLAG;
	P2OUT &= ~GRNFLAG;
	P2OUT &= ~LCDDATA;

	// for GPIO interrupt
	P1OUT |= (ENTER + UP + REDTAKE + GRNTAKE); // pullup
	P1REN |= (ENTER + UP + REDTAKE + GRNTAKE); // enable resistor
	P1IES |= (ENTER + UP + REDTAKE + GRNTAKE); // set for 1->0 transition
	P1IFG &= ~(ENTER + UP + REDTAKE + GRNTAKE);// clear interrupt flag
	P1IE  |= (ENTER + UP + REDTAKE + GRNTAKE); // enable interrupt

}


// send a single nibble (numbered 1 or 2) to this function to use it as a command and toggle Enable pin
void cmdtoLCD(char whichnibble, char nibble){
	// turn the given char into a 4-bit nibble
	if(whichnibble == 1){
		nibble = (nibble & 0xF0) >> 4;
	}
	else if (whichnibble == 2){
		nibble = nibble & 0x0F;
	}

	// send the nibble to the LCD and reset Enable
	P1OUT &= ~LCDRS;	// because it's a command
	P1OUT |= LCDE;
	P2OUT &= ~LCDDATA;
	P2OUT |= nibble;
	P1OUT &= ~LCDE;
}

// send a single nibble (numbered 1 or 2) to this function to print it to LCD and toggle Enable pin
void chartoLCD(char whichnibble, char nibble){
	// turn the given int into a 4-bit nibble
	if(whichnibble == 1){
		nibble = (nibble & 0xF0) >> 4;
	}
	else if (whichnibble == 2){
		nibble = nibble & 0x0F;
	}

	// send the nibble to the LCD and reset Enable
	P1OUT |= (LCDRS + LCDE);	// because it's data
	P2OUT &= ~LCDDATA;
	P2OUT |= nibble;
	P1OUT &= ~LCDE;
}

// send whichnibble (1 or 2), and an address in row (1 or 2) and col (1-16) and moves the cursor there by calling cmdtoLCD, two iterations
void gotoLCDpos(char whichnibble, char row, char col){
	// turn into a DDRAM address (hex)
	LCDlocation = 0x80;
	if (row == 0x02){
		LCDlocation += 0x40;
	}
	LCDlocation += (col - 0x01);

	// execute the command
	cmdtoLCD(whichnibble, LCDlocation);
}

void LCD_LUT(char num2print){
	switch(num2print){
		case 0:{
			lownibble = 0x00;
			break;
		}
		case 1:{
			lownibble = 0x01;
			break;
		}
		case 2:{
			lownibble = 0x02;
			break;
		}
		case 3:{
			lownibble = 0x03;
			break;
		}
		case 4:{
			lownibble = 0x04;
			break;
		}
		case 5:{
			lownibble = 0x05;
			break;
		}
		case 6:{
			lownibble = 0x06;
			break;
		}
		case 7:{
			lownibble = 0x07;
			break;
		}
		case 8:{
			lownibble = 0x08;
			break;
		}
		case 9:{
			lownibble = 0x09;
			break;
		}
	}
}

// initialization of the LCD module, called __ times by the WDT interrupt handler
void LCD_init(char whichcase){
	switch(whichcase){
		//-----Wait for LCD Startup-----
		case 0: {	// set Enable to normal high, RS/RW to instruction (0/0)
			P1OUT |= LCDE;
			P1OUT &= ~(LCDRW + LCDRS);
			P2OUT &= ~LCDDATA;
			break;
		}
		case 1: {
			break;
		}
		case 2: {
			break;
		}
		case 3: {	// wait 4 WDT cycles for LCD to catch up
			break;
		}

		//-----Setup Data Transfer and Put LCD in 4 Bit Mode-----
		case 4: {
		}
		case 5: {
		}
		case 6: {	// Write 00110000 to LCD 3 times (hence the break-less cases above), to set up data transfer
			cmdtoLCD(0x02, 0x03);
			break;
		}
		case 7: {	// write 00100000 to enable 4 bit mode
			cmdtoLCD(0x02, 0x02);
			break;
		}

		//-----Clear the Display and Reset the Cursor-----
		case 8: {	// write 0000...
			cmdtoLCD(0x02, 0x00);
			break;
		}
		case 9: {	// ...then 0001 to clear the display
			cmdtoLCD(0x02, 0x01);
			break;
		}
		case 10: {	// write 0000...
			cmdtoLCD(0x02, 0x00);
			break;
		}
		case 11: {	// ...then 1100 to move cursor to right place, but turn off visible cursor and blink
			cmdtoLCD(0x02, 0x0C);
			break;
		}

		//-----Turn 2 Lines On-----
		case 12: {	// write 0010...
			cmdtoLCD(0x02, 0x02);
			break;
		}
		case 13: {	// ...then 1000 to turn both lines on
			cmdtoLCD(0x02, 0x08);
			break;
		}

		//-----Write "G 00 " for Green Score-----
		case 14: {	// write 0100...
			chartoLCD(0x02, 0x04);
			break;
		}
		case 15: {	// ...then 0111 to write G
			chartoLCD(0x02, 0x07);
			break;
		}
		case 16: {	// write 0010...
			chartoLCD(0x02, 0x02);
			break;
		}
		case 17: {	// ...then 0000 to write _
			chartoLCD(0x02, 0x00);
			break;
		}
		case 18: {	// write 0011...
			chartoLCD(0x02, 0x03);
			break;
		}
		case 19: {	// ...then 0000 to write 0
			chartoLCD(0x02, 0x00);
			break;
		}
		case 20: {	// write 0011...
			chartoLCD(0x02, 0x03);
			break;
		}
		case 21: {	// ...then 0000 to write 0
			chartoLCD(0x02, 0x00);
			break;
		}
		case 22: {	// write 0010...
			chartoLCD(0x02, 0x02);
			break;
		}
		case 23: {	// ...then 0000 to write _
			chartoLCD(0x02, 0x00);
			break;
		}
		case 24: {	// write 0101...
			chartoLCD(0x02, 0x05);
			break;
		}

		//-----Write "T00:00 " for GameTime-----
		case 25: {	// ...then 0100 to write T
			chartoLCD(0x02, 0x04);
			break;
		}
		case 26: {	// write 0011...
			chartoLCD(0x02, 0x03);
			break;
		}
		case 27: {	// ...then 0000 to write 0
			chartoLCD(0x02, 0x00);
			break;
		}
		case 28: {	// write 0011...
			chartoLCD(0x02, 0x03);
			break;
		}
		case 29: {	// ...then 0010 to write 2
			chartoLCD(0x02, 0x02);
			break;
		}
		case 30: {	// write 0011...
			chartoLCD(0x02, 0x03);
			break;
		}
		case 31: {	// ...then 1010 to write :
			chartoLCD(0x02, 0x0A);
			break;
		}
		case 32: {	// write 0011...
			chartoLCD(0x02, 0x03);
			break;
		}
		case 33: {	// ...then 0000 to write 0
			chartoLCD(0x02, 0x00);
			break;
		}
		case 34: {	// write 0011...
			chartoLCD(0x02, 0x03);
			break;
		}
		case 35: {	// ...then 0000 to write 0
			chartoLCD(0x02, 0x00);
			break;
		}
		case 36: {	// write 0010...
			chartoLCD(0x02, 0x02);
			break;
		}
		case 37: {	// ...then 0000 to write _
			chartoLCD(0x02, 0x00);
			break;
		}
		case 38: {	// write 0101...
			chartoLCD(0x02, 0x05);
			break;
		}

		//-----Write "R 00" for Red Score-----
		case 39: {	// ...then 0010 to write R
			chartoLCD(0x02, 0x02);
			break;
		}
		case 40: {	// write 0010...
			chartoLCD(0x02, 0x02);
			break;
		}
		case 41: {	// ...then 0000 to write _
			chartoLCD(0x02, 0x00);
			break;
		}
		case 42: {	// write 0011...
			chartoLCD(0x02, 0x03);
			break;
		}
		case 43: {	// ...then 0000 to write 0
			chartoLCD(0x02, 0x00);
			break;
		}
		case 44: {	// write 0011...
			chartoLCD(0x02, 0x03);
			break;
		}
		case 45: {	// ...then 0000 to write 0
			chartoLCD(0x02, 0x00);
			break;
		}

		//-----Go To Second Line, Write 0 for Green TakeTime-----
		case 46: {	// go to 2nd row, 3rd column
			gotoLCDpos(letternibble, 0x02, 0x03);
			letternibble++;
			break;
		}
		case 47: {
			gotoLCDpos(letternibble, 0x02, 0x03);
			letternibble--;
			break;
		}
		case 48: {	// write 0011...
			chartoLCD(0x02, 0x03);
			break;
		}
		case 49: {	// ...then 0000 to write 0
			chartoLCD(0x02, 0x00);
			break;
		}

		//-----Write "F0:00" for FlagTime-----
		case 50: {	// go to 2nd line, 7th column
			gotoLCDpos(letternibble, 0x02, 0x07);
			letternibble++;
			break;
		}
		case 51: {
			gotoLCDpos(letternibble, 0x02, 0x07);
			letternibble--;
			break;
		}
		case 52: {	// write 0100...
			chartoLCD(0x02, 0x04);
			break;
		}
		case 53: {	// ...then 0110 to write F
			chartoLCD(0x02, 0x06);
			break;
		}
		case 54: {	// write 0011...
			chartoLCD(0x02, 0x03);
			break;
		}
		case 55: {	// ...then 0000 to write 0
			chartoLCD(0x02, 0x00);
			break;
		}
		case 56: {	// write 0011...
			chartoLCD(0x02, 0x03);
			break;
		}
		case 57: {	// ...then 1010 to write :
			chartoLCD(0x02, 0x0A);
			break;
		}
		case 58: {	// write 0011...
			chartoLCD(0x02, 0x03);
			break;
		}
		case 59: {	// ...then 0000 to write 0
			chartoLCD(0x02, 0x00);
			break;
		}
		case 60: {	// write 0011...
			chartoLCD(0x02, 0x03);
			break;
		}
		case 61: {	// ...then 0000 to write 0
			chartoLCD(0x02, 0x00);
			break;
		}

		//-----Write "0" for Red TakeTime-----
		case 62: {	// go to 2nd line, 15th column
			gotoLCDpos(letternibble, 0x02, 0x0F);
			letternibble++;
			break;
		}
		case 63: {
			gotoLCDpos(letternibble, 0x02, 0x0F);
			letternibble--;
			break;
		}
		case 64: {	// write 0011...
			chartoLCD(0x02, 0x03);
			break;
		}
		case 65: {	// ...then 0000 to write 0
			chartoLCD(0x02, 0x00);
			break;
		}

		//-----Turn off Scrolling-----
		case 66: {	// write 0001...
			cmdtoLCD(0x02, 0x01);
			break;
		}
		case 67: { // ...then 0000 to turn off scrolling between character writing
			cmdtoLCD(0x02, 0x00);
			break;
		}
	}
}

void interrupt gpio_handler(){
	switch (state)
	{
		case 'i':
			P1IES |= (ENTER + UP + REDTAKE + GRNTAKE);
			P1IFG &= ~(ENTER + UP + REDTAKE + GRNTAKE);
			break;

		case 0: //game initialization
			//on UP button hit, increment TIME (MINUTE) (if 20, go to 2) - GPIO
			if(P1IFG & UP){
				P2OUT &= ~(REDFLAG + GRNFLAG);
				if(begin_update == 0){
					if((MINUTES == 15)||(MINUTES == 0))
						MINUTES = 2;
					else
						MINUTES++;

					singles_digit = MINUTES % 10;
					tens_digit = MINUTES / 10;
					begin_update = 1;
				}
			}
			//on ENTER button hit, go to state 1 - GPIO
			if(P1IFG & ENTER){
				P2OUT &= ~(REDFLAG + GRNFLAG);
				if(begin_update == 0){
					state = 1;
				}
			}
			P1IES |= (ENTER + UP + REDTAKE + GRNTAKE);
			P1IFG &= ~(ENTER + UP + REDTAKE + GRNTAKE);
			break;

		case 1: //signal start up
			//no GPIO activity
			P1IFG &= ~(ENTER + UP + REDTAKE + GRNTAKE);
			break;

		case 2: //initial waiting state
			if(P1IFG & REDTAKE){
				//unnecessary LED indicator
				P2OUT |= REDFLAG;
				P2OUT &= ~GRNFLAG;

				last_state = 2;
				state = 3;

				P1IES &= ~REDTAKE;	//switch to 0->1 transition
				P1IFG &= ~REDTAKE;
			}
			if(P1IFG & GRNTAKE){
				//unnecessary LED indicator
				P2OUT |= GRNFLAG;
				P2OUT &= ~REDFLAG;

				last_state = 2;
				state = 4;

				P1IES &= ~GRNTAKE;	//switch to 0->1 transition
				P1IFG &= ~GRNTAKE;
			}
			P1IFG &= ~(ENTER + UP + REDTAKE + GRNTAKE);
			break;

		case 3: //RED tries to take
			if(P1IES & ~REDTAKE)
				REDPRESS = 0;
			else if(P1IES & REDTAKE)
				REDPRESS = 1;

			if(P1IFG & REDTAKE){
				if(REDPRESS == 0){
					if(second_ctr < 5){ //RED let go; RED failed to take the flag
						second_ctr = 0;			//reset time counters
						wdt_ctr = 0;
						OWN_SECONDS = 0;
						OWN_MINUTES = 0;
						P2OUT &= ~REDFLAG;
						begin_update = 3;

//						P2OUT &= ~LCDB3;	//random...;

						state = last_state;
					}
				}
			}
			else{
				state = 3;
			}
			P1IES |= (ENTER + UP + REDTAKE + GRNTAKE);	//switch back to 1->0 transition
			P1IFG &= ~(ENTER + UP + REDTAKE + GRNTAKE);
			break;

		case 4: //GRN tries to take
			if(P1IES & ~GRNTAKE)
				GRNPRESS = 0;
			else if(P1IES & GRNTAKE)
				GRNPRESS = 1;

			if(P1IFG & GRNTAKE){
				if(GRNPRESS == 0){
					if(second_ctr < 5){ //GRN let go; GRN failed to take the flag
						second_ctr = 0;			//reset time counters
						wdt_ctr = 0;
						OWN_SECONDS = 0;
						OWN_MINUTES = 0;
						P2OUT &= ~GRNFLAG;
						begin_update = 4;

//						P2OUT &= ~LCDB3;	//random...

						state = last_state;
					}
				}
			}
			else{
				state = 4;
			}
			P1IES |= (ENTER + UP + REDTAKE + GRNTAKE);	//switch back to 1->0 transition
			P1IFG &= ~(ENTER + UP + REDTAKE + GRNTAKE);
			break;

		case 5: //RED owns flag
			//if we're still in this state, RED hasn't won yet
			// (hasn't been 2 minutes yet)
			if(P1IFG & GRNTAKE){	//GRN hits the flag target GRNTAKE
				begin_update = 6;
				state = 9;			//move to "GRN tries to take" state (4)
			}
			else{
				last_state = 5;
				state = 5;
			}
			P1IES |= (ENTER + UP + REDTAKE + GRNTAKE);
			P1IFG &= ~(ENTER + UP + REDTAKE + GRNTAKE);
			break;

		case 6: //GRN owns flag
			if(P1IFG & REDTAKE){	//RED hits the flag target REDTAKE
				begin_update = 6;
				state = 8;			//move to "RED tries to take" state (3)
			}
			else{
				last_state = 6;
				state = 6;
			}
			P1IES |= (ENTER + UP + REDTAKE + GRNTAKE);
			P1IFG &= ~(ENTER + UP + REDTAKE + GRNTAKE);
			break;

		case 7: //Game Over
			if(P1IFG & UP){	//restart game when ENTER pressed
				state = 0;
				last_state = 0;

				RED_SCORE = 0;
				GRN_SCORE = 0;
			}
			//reset interrupt flags for the next game
			P1IES |= (ENTER + UP + REDTAKE + GRNTAKE);
			P1IFG &= ~(ENTER + UP + REDTAKE + GRNTAKE);
			break;

		case 8: //RED tries to take when GRN owns FLAG
			if(P1IES & ~REDTAKE)
				REDPRESS = 0;
			else if(P1IES & REDTAKE)
				REDPRESS = 1;

			if(REDPRESS == 0){
				if(second_ctr < 5){ //RED let go; RED failed to take the flag
					second_ctr = 0;			//reset time counters
					wdt_ctr = 0;
//					OWN_SECONDS = 0;
//					OWN_MINUTES = 0;
					P2OUT &= ~REDFLAG;
					begin_update = 3;

					state = 6;
				}
			}
			else{
				state = 8;
			}
			P1IES |= (ENTER + UP + REDTAKE + GRNTAKE);	//switch back to 1->0 transition
			P1IFG &= ~(ENTER + UP + REDTAKE + GRNTAKE);
			break;

		case 9: //GRN tries to take when RED owns FLAG
			if(P1IES & ~GRNTAKE)
				GRNPRESS = 0;
			else if(P1IES & GRNTAKE)
				GRNPRESS = 1;

			if(P1IFG & GRNTAKE){
				if(GRNPRESS == 0){
					if(second_ctr < 5){ //GRN let go; GRN failed to take the flag
						second_ctr = 0;			//reset time counters
						wdt_ctr = 0;
//						OWN_SECONDS = 0;
//						OWN_MINUTES = 0;
						P2OUT &= ~GRNFLAG;
						begin_update = 4;

						state = 5;
					}
				}
			}
			else{
				state = 9;
			}
			P1IES |= (ENTER + UP + REDTAKE + GRNTAKE);	//switch back to 1->0 transition
			P1IFG &= ~(ENTER + UP + REDTAKE + GRNTAKE);
			break;
	}
}
ISR_VECTOR(gpio_handler,".int02") // declare interrupt vector

// ===== Watchdog Timer Interrupt Handler =====
// This event handler is called to handle the watchdog timer interrupt,
//    which is occurring regularly at intervals of 32K/1MHz ~= 32ms.

void interrupt WDT_interval_handler(){

	switch (state)
	{
		case 'i': //start LCD
			LCD_init(whichcase);
			if(whichcase < INIT_CNT){
				whichcase++;
			}
			else if(whichcase == INIT_CNT)
			{
				whichcase = 0;
				state = 0;
			}
			break;

		case 0: //game initialization
			//update LCD when MINUTES changes
			if(begin_update == 1)
			{
				if(whichcase == 0){
					LCD_LUT(tens_digit);
					gotoLCDpos(1, 1, 7);
				}
				else if(whichcase == 1){
					gotoLCDpos(2, 1, 7);
				}
				else if(whichcase == 2){
					chartoLCD(1, highnibble);
				}
				else if(whichcase == 3){
					chartoLCD(2, lownibble);
				}

				else if(whichcase == 4){
					LCD_LUT(singles_digit);
					gotoLCDpos(1, 1, 8);
				}
				else if(whichcase == 5){
					gotoLCDpos(2, 1, 8);
				}
				else if(whichcase == 6){
					chartoLCD(1, highnibble);
				}
				else if(whichcase == 7){
					chartoLCD(2, lownibble);
					begin_update = 0;
				}

				if(whichcase < 7){
					whichcase++;
				}
				else{
					whichcase = 0;
				}
			}
			break;

		case 1: //signal start up
			//flash (toggle) all LEDs every second - WDT
			if(wdt_ctr < ONE_SEC)		//until a second is reached,
				wdt_ctr++;						//keep counting WDT intervals
			else{						//at every second,
				P2OUT ^= (REDFLAG + GRNFLAG);	//toggle FLAG LEDs
				wdt_ctr = 0;					//reset to start count for the new cycle
				second_ctr++;					//increase number of seconds that passed
			}
			//when 20 seconds (20 seconds / time of 1 cycle = N WDT cycles), go to state 2 - WDT
			if(second_ctr == 6){
				second_ctr = 0;					//reset time counters (just in case)
				wdt_ctr = 0;
				P2OUT &= ~(REDFLAG + GRNFLAG);	//FLAG is un-owned so has no color
				state = 2;						//move onto next state
				begin_countdown = 1;			//start the game timer
			}
			break;

/*		case 2: //initial waiting state
			//nothing special happening here. just waiting for GPIO activity
			break;
*/
		case 3: //RED tries to take
			if(wdt_ctr < ONE_SEC)		//until a second is reached,
				wdt_ctr++;					//keep counting WDT intervals
			else{						//at every second,
				P2OUT ^= REDFLAG;			//blink REDFLAG LEDs - RED is about to take FLAG
				wdt_ctr = 0;				//reset to start count for the new cycle
				second_ctr++;				//increase number of seconds that passed
				begin_update = 3;
			}
			//when 5 seconds (5 seconds / time of 1 cycle = M WDT cycles), go to state 5 - WDT
			if(second_ctr > 5){
				second_ctr = 0;			//reset time counters (just in case)
				second_ctr_flg = 0;
				wdt_ctr = 0;
				wdt_ctr_flg = 0;
				P2OUT |= REDFLAG;		//FLAG is now owned by RED
				P2OUT &= ~GRNFLAG;
				begin_update = 3;

				state = 5;				//move onto state 5
			}
			break;

		case 4: //GRN tries to take
			if(wdt_ctr < ONE_SEC)		//until a second is reached,
				wdt_ctr++;					//keep counting WDT intervals
			else{						//at every second,
				P2OUT ^= GRNFLAG;			//blink GRNFLAG LEDs - GRN is about to take FLAG
				wdt_ctr = 0;					//reset to start count for the new cycle
				second_ctr++;					//increase number of seconds that passed
				begin_update = 4;
			}
			//when 5 seconds (5 seconds / time of 1 cycle = M WDT cycles), go to state 5 - WDT
			if(second_ctr > 5){
				second_ctr = 0;			//reset time counters (just in case)
				second_ctr_flg = 0;
				wdt_ctr = 0;
				wdt_ctr_flg = 0;
				P2OUT |= GRNFLAG;		//FLAG is now owned by GRN
				P2OUT &= ~REDFLAG;		//make sure REDFLAG is off
				begin_update = 4;

				state = 6;				//move onto state 6
			}
			break;

		case 5: //RED owns flag
			if(wdt_ctr_flg < ONE_SEC)		//until a second is reached,
				wdt_ctr_flg++;						//keep counting WDT intervals
			else{						//at every second,
				P2OUT |= REDFLAG;			//make sure RED owns the FLAG
				P2OUT &= ~GRNFLAG;			// and GRN doesn't

				wdt_ctr_flg = 0;				//reset to start count for the new cycle
				second_ctr_flg++;				//increase number of seconds that passed
				OWN_SECONDS++;				//increment OWN_TIME (SECOND)
				begin_update = 5;
			}
			//increment RED_SCORE by 1 every 10 seconds
			if(second_ctr_flg == 10){
				RED_SCORE++;
				second_ctr = 0;			//reset time counters
				second_ctr_flg = 0;
				wdt_ctr = 0;			// (just in case)
				wdt_ctr_flg = 0;
				begin_update = 7;
			}
			//increment OWN_TIME (SECOND) every second (if 59 -> 00, increment MINUTE) - WDT
			if(OWN_SECONDS == 60)
			{
				OWN_SECONDS = 0;
				OWN_MINUTES++;
				begin_update = 6;
			}
			if(OWN_MINUTES == 2)
			{
				OWN_SECONDS = 0;
				OWN_MINUTES = 0;
				WINNER = 'R';
				begin_countdown = 0;
				state = 7;
			}
			break;

		case 6: //GRN owns flag
			if(wdt_ctr_flg < ONE_SEC)		//until a second is reached,
				wdt_ctr_flg++;						//keep counting WDT intervals
			else{						//at every second,
				P2OUT |= GRNFLAG;			//make sure GRN owns the FLAG
				P2OUT &= ~REDFLAG;			// and RED doesn't

				wdt_ctr_flg = 0;				//reset to start count for the new cycle
				second_ctr_flg++;				//increase number of seconds that passed
				OWN_SECONDS++;				//increment OWN_TIME (SECOND)
				begin_update = 5;
			}
			//increment RED_SCORE by 1 every 10 seconds
			if(second_ctr_flg == 10){
				GRN_SCORE++;
				second_ctr = 0;			//reset time counters
				second_ctr_flg = 0;
				wdt_ctr = 0;			// (just in case)
				wdt_ctr_flg = 0;
				begin_update = 8;
			}
			//increment OWN_TIME (SECOND) every second (if 59 -> 00, increment MINUTE) - WDT
			if(OWN_SECONDS == 60)
			{
				OWN_SECONDS = 0;
				OWN_MINUTES++;
				begin_update = 6;
			}
			if(OWN_MINUTES == 2)
			{
				OWN_SECONDS = 0;
				OWN_MINUTES = 0;
				WINNER = 'G';
				begin_countdown = 0;
				state = 7;
			}
			break;

		case 7: //Game Over
			if(WINNER == 'R'){
				P2OUT |= REDFLAG;
				P2OUT &= ~GRNFLAG;
			}
			else if(WINNER == 'G'){
				P2OUT |= GRNFLAG;
				P2OUT &= ~REDFLAG;
			}
			else if(WINNER == 'T'){
				P2OUT |= (REDFLAG + GRNFLAG);
			}

			//Restart Game and Reset Everything
			begin_countdown = 0;

			MINUTES = 2;
			SECONDS = 0;
			wdt_ctr_timer = 0;

			wdt_ctr = 0;
			second_ctr = 0;

			OWN_SECONDS = 0;
			OWN_MINUTES = 0;

			REDPRESS = 0;
			GRNPRESS = 0;

			RED_SCORE = 0;
			GRN_SCORE = 0;

			last_state = 7;
			state = 'i';
			break;

		case 8: //RED tries to take when GRN owns FLAG
			if(wdt_ctr < ONE_SEC)		//until a second is reached,
				wdt_ctr++;					//keep counting WDT intervals
			else{						//at every second,
				P2OUT ^= REDFLAG;			//blink REDFLAG LEDs - RED is about to take FLAG
				wdt_ctr = 0;				//reset to start count for the new cycle
				second_ctr++;				//increase number of seconds that passed
				begin_update = 3;
			}
			//when 5 seconds (5 seconds / time of 1 cycle = M WDT cycles), go to state 5 - WDT
			if(second_ctr > 5){
				second_ctr = 0;
				second_ctr_flg = 0;
				wdt_ctr = 0;
				wdt_ctr_flg = 0;
				OWN_SECONDS = 0;
				OWN_MINUTES = 0;
				P2OUT |= REDFLAG;		//FLAG is now owned by RED
				P2OUT &= ~GRNFLAG;
				begin_update = 3;

				state = 5;				//move onto state 5
			}

			if(wdt_ctr_flg < ONE_SEC)		//until a second is reached,
				wdt_ctr_flg++;						//keep counting WDT intervals
			else{						//at every second,
				P2OUT |= GRNFLAG;			//make sure GRN owns the FLAG
				P2OUT &= ~REDFLAG;			// and RED doesn't

				wdt_ctr_flg = 0;				//reset to start count for the new cycle
				second_ctr_flg++;				//increase number of seconds that passed
				OWN_SECONDS++;				//increment OWN_TIME (SECOND)
				begin_update = 5;
			}
			//increment RED_SCORE by 1 every 10 seconds
			if(second_ctr_flg == 10){
				GRN_SCORE++;
				second_ctr_flg = 0;			//reset time counters
				wdt_ctr_flg = 0;			// (just in case)
				begin_update = 8;
			}
			//increment OWN_TIME (SECOND) every second (if 59 -> 00, increment MINUTE) - WDT
			if(OWN_SECONDS == 60)
			{
				OWN_SECONDS = 0;
				OWN_MINUTES++;
				begin_update = 6;
			}
			if(OWN_MINUTES == 2)
			{
				OWN_SECONDS = 0;
				OWN_MINUTES = 0;
				WINNER = 'G';
				begin_countdown = 0;
				state = 7;
			}
			break;

		case 9: //GRN tries to take when RED owns FLAG
			if(wdt_ctr < ONE_SEC)		//until a second is reached,
				wdt_ctr++;					//keep counting WDT intervals
			else{						//at every second,
				P2OUT ^= GRNFLAG;			//blink GRNFLAG LEDs - GRN is about to take FLAG
				wdt_ctr = 0;					//reset to start count for the new cycle
				second_ctr++;					//increase number of seconds that passed
				begin_update = 4;
			}
			//when 5 seconds (5 seconds / time of 1 cycle = M WDT cycles), go to state 5 - WDT
			if(second_ctr > 5){
				second_ctr = 0;			//reset time counters (just in case)
				second_ctr_flg = 0;
				wdt_ctr = 0;
				wdt_ctr_flg = 0;
				OWN_SECONDS = 0;
				OWN_MINUTES = 0;
				P2OUT |= GRNFLAG;		//FLAG is now owned by GRN
				P2OUT &= ~REDFLAG;		//make sure REDFLAG is off
				begin_update = 4;

				state = 6;				//move onto state 6
			}

			if(wdt_ctr_flg < ONE_SEC)		//until a second is reached,
				wdt_ctr_flg++;						//keep counting WDT intervals
			else{						//at every second,
				P2OUT |= GRNFLAG;			//make sure GRN owns the FLAG
				P2OUT &= ~REDFLAG;			// and RED doesn't

				wdt_ctr_flg = 0;				//reset to start count for the new cycle
				second_ctr_flg++;				//increase number of seconds that passed
				OWN_SECONDS++;				//increment OWN_TIME (SECOND)
				begin_update = 5;
			}
			//increment RED_SCORE by 1 every 10 seconds
			if(second_ctr_flg == 10){
				GRN_SCORE++;
				second_ctr_flg = 0;			//reset time counters
				wdt_ctr = 0;			// (just in case)
				begin_update = 8;
			}
			//increment OWN_TIME (SECOND) every second (if 59 -> 00, increment MINUTE) - WDT
			if(OWN_SECONDS == 60)
			{
				OWN_SECONDS = 0;
				OWN_MINUTES++;
				begin_update = 6;
			}
			if(OWN_MINUTES == 2)
			{
				OWN_SECONDS = 0;
				OWN_MINUTES = 0;
				WINNER = 'G';
				begin_countdown = 0;
				state = 7;
			}
			break;

	}

	if(begin_countdown == 1)	// game counter gets incremented and displayed
	{
		if(wdt_ctr_timer < ONE_SEC)		//until a second is reached,
			wdt_ctr_timer++;					//keep counting WDT intervals
		else{						//at every second,
			begin_update = 1;
			wdt_ctr_timer = 0;					//reset to start count for the new cycle
			if(SECONDS == 0){
				SECONDS = 59;
				MINUTES = MINUTES - 1;
				begin_update = 2;
			}
			else
				SECONDS = SECONDS - 1;					//increase number of seconds that passed
		}
		//when 5 seconds (5 seconds / time of 1 cycle = M WDT cycles), go to state 5 - WDT
		if((MINUTES == 0) && (SECONDS == 0)){
			if(RED_SCORE > GRN_SCORE)
			{
				WINNER = 'R';
			}
			else if(GRN_SCORE > RED_SCORE)
			{
				WINNER = 'G';
			}
			else if(GRN_SCORE == RED_SCORE)
			{
				WINNER = 'T';
			}
			begin_countdown = 0;
			state = 7;				//move onto state 7 - GAME OVER
		}

		if(begin_update == 8)	//updates GRN_SCORE
		{
			if(whichcase == 0){
				tens_digit = GRN_SCORE / 10;
				singles_digit = GRN_SCORE % 10;

//				LCD_LUT(tens_digit);
				gotoLCDpos(1, 1, 3);
			}
			else if(whichcase == 1){
				gotoLCDpos(2, 1, 3);
			}
			else if(whichcase == 2){
				chartoLCD(1, highnibble);
			}
			else if(whichcase == 3){
				chartoLCD(2, tens_digit);
			}

			else if(whichcase == 4){
//				LCD_LUT(singles_digit);
				gotoLCDpos(1, 1, 4);
			}
			else if(whichcase == 5){
				gotoLCDpos(2, 1, 4);
			}
			else if(whichcase == 6){
				chartoLCD(1, highnibble);
			}
			else if(whichcase == 7){
				chartoLCD(2, singles_digit);
				begin_update = 6;
			}

			if(whichcase < 7){
				whichcase++;
			}
			else if(whichcase == 7){
				whichcase = 0;
			}
		}
		else if(begin_update == 7)	//updates RED_SCORE
		{
			if(whichcase == 0){
				tens_digit = RED_SCORE / 10;
				singles_digit = RED_SCORE % 10;

//				LCD_LUT(tens_digit);
				gotoLCDpos(1, 1, 15);
			}
			else if(whichcase == 1){
				gotoLCDpos(2, 1, 15);
			}
			else if(whichcase == 2){
				chartoLCD(1, highnibble);
			}
			else if(whichcase == 3){
				chartoLCD(2, tens_digit);
			}

			else if(whichcase == 4){
//				LCD_LUT(singles_digit);
				gotoLCDpos(1, 1, 16);
			}
			else if(whichcase == 5){
				gotoLCDpos(2, 1, 16);
			}
			else if(whichcase == 6){
				chartoLCD(1, highnibble);
			}
			else if(whichcase == 7){
				chartoLCD(2, singles_digit);
				begin_update = 6;
			}

			if(whichcase < 7){
				whichcase++;
			}
			else if(whichcase == 7){
				whichcase = 0;
			}
		}
		else if(begin_update == 6)	//updates time (minutes) FLAG OWNED (OWN_MINUTES)
		{
			if(whichcase == 0){
//				LCD_LUT(OWN_MINUTES);
				gotoLCDpos(1, 2, 8);
			}
			else if(whichcase == 1){
				gotoLCDpos(2, 2, 8);
			}
			else if(whichcase == 2){
				chartoLCD(1, highnibble);
			}
			else if(whichcase == 3){
				chartoLCD(2, OWN_MINUTES);
				begin_update = 5;
			}

			if(whichcase < 3){
				whichcase++;
			}
			else if(whichcase == 3){
				whichcase = 0;
			}
		}
		else if(begin_update == 5)	//updates time (seconds) FLAG OWNED (OWN_SECONDS)
		{
			if(whichcase == 0){
				tens_digit = OWN_SECONDS / 10;
				singles_digit = OWN_SECONDS % 10;

//				LCD_LUT(tens_digit);
				gotoLCDpos(1, 2, 10);
			}
			else if(whichcase == 1){
				gotoLCDpos(2, 2, 10);
			}
			else if(whichcase == 2){
				chartoLCD(1, highnibble);
			}
			else if(whichcase == 3){
				chartoLCD(2, tens_digit);
			}

			else if(whichcase == 4){
//				LCD_LUT(singles_digit);
				gotoLCDpos(1, 2, 11);
			}
			else if(whichcase == 5){
				gotoLCDpos(2, 2, 11);
			}
			else if(whichcase == 6){
				chartoLCD(1, highnibble);
			}
			else if(whichcase == 7){
				chartoLCD(2, singles_digit);
				begin_update = 2;
			}

			if(whichcase < 7){
				whichcase++;
			}
			else if(whichcase == 7){
				whichcase = 0;
			}
		}
		else if(begin_update == 4)	//updates time until GRN seizes FLAG
		{
			if(whichcase == 0){
//				LCD_LUT(tens_digit);
				gotoLCDpos(1, 2, 3);
			}
			else if(whichcase == 1){
				gotoLCDpos(2, 2, 3);
			}
			else if(whichcase == 2){
				chartoLCD(1, highnibble);
			}
			else if(whichcase == 3){
				chartoLCD(2, second_ctr);
				begin_update = 2;
			}

			if(whichcase < 3){
				whichcase++;
			}
			else if(whichcase == 3){
				whichcase = 0;
			}
		}
		else if(begin_update == 3)	//updates time until RED seizes FLAG
		{
			if(whichcase == 0){
//				LCD_LUT(second_ctr);
				gotoLCDpos(1, 2, 15);
			}
			else if(whichcase == 1){
				gotoLCDpos(2, 2, 15);
			}
			else if(whichcase == 2){
				chartoLCD(1, highnibble);
			}
			else if(whichcase == 3){
				chartoLCD(2, second_ctr);
				begin_update = 2;
			}

			if(whichcase < 3){
				whichcase++;
			}
			else if(whichcase == 3){
				whichcase = 0;
			}
		}
		else if(begin_update == 2)	//updates MINUTES for TIMER
		{
			if(whichcase == 0){
				tens_digit = MINUTES / 10;
				singles_digit = MINUTES % 10;

//				LCD_LUT(tens_digit);
				gotoLCDpos(1, 1, 7);
			}
			else if(whichcase == 1){
				gotoLCDpos(2, 1, 7);
			}
			else if(whichcase == 2){
				chartoLCD(1, highnibble);
			}
			else if(whichcase == 3){
				chartoLCD(2, tens_digit);
			}

			else if(whichcase == 4){
//				LCD_LUT(singles_digit);
				gotoLCDpos(1, 1, 8);
			}
			else if(whichcase == 5){
				gotoLCDpos(2, 1, 8);
			}
			else if(whichcase == 6){
				chartoLCD(1, highnibble);
			}
			else if(whichcase == 7){
				chartoLCD(2, singles_digit);
				begin_update = 1;
			}

			if(whichcase < 7){
				whichcase++;
			}
			else if(whichcase == 7){
				whichcase = 0;
			}
		}
		else if(begin_update == 1)
		{
			if(whichcase == 0){
				tens_digit = SECONDS / 10;
				singles_digit = SECONDS % 10;

//				LCD_LUT(tens_digit);
				gotoLCDpos(1, 1, 10);
			}
			else if(whichcase == 1){
				gotoLCDpos(2, 1, 10);
			}
			else if(whichcase == 2){
				chartoLCD(1, highnibble);
			}
			else if(whichcase == 3){
				chartoLCD(2, tens_digit);
			}

			else if(whichcase == 4){
				LCD_LUT(singles_digit);
				gotoLCDpos(1, 1, 11);
			}
			else if(whichcase == 5){
				gotoLCDpos(2, 1, 11);
			}
			else if(whichcase == 6){
				chartoLCD(1, highnibble);
			}
			else if(whichcase == 7){
				chartoLCD(2, singles_digit);
				begin_update = 0;
			}

			if(whichcase < 7){
				whichcase++;
			}
			else if(whichcase == 7){
				whichcase = 0;
			}
		}

	}

}
// DECLARE WDT_interval_handler as handler for interrupt 10
ISR_VECTOR(WDT_interval_handler, ".int10")
