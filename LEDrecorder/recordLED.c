/***********************************************************************
	Jenny Hoac
	October 2, 2013
	
	The program records a series of user input button presses. 
	The button press sequence is "played back" to the user in the form of
	LED blinks. This happens if the user is inactive for a set amount of time
	or if record memory is filled up.
	User can then record a new sequence after playback completion.

 ***********************************************************************/

#include <msp430.h> 

#define RED	0x01			// mask to turn red LED on
#define GREEN 0x40			// mask to turn green LED on
#define BUTTON 0x08			// mask for push button
int currentMode;			// changes between record mode (0) and playback mode (1)
int lastButtonState;		// flag for transition between pressing button and letting go
int playLight;				// flag for if light is on or off in playback mode
int recordCounter;			// iterator for array and determining when to stop recording
int recordMemory[40] = {0};	// array that keeps track of button press sequence

int main(void) {
    WDTCTL = (WDTPW + WDTTMSEL + WDTCNTCL + 0 + 1);
    IE1 |= WDTIE;	// enable the WDT interrupt

    // initialize the I/O ports
    P1DIR |= (RED+GREEN);
    P1REN = BUTTON;
    P1OUT = BUTTON;

    currentMode = 1;					// start in playback mode
    recordCounter = 0;					// initially in wait mode (waiting to record)
    lastButtonState = 0;				// button is initially not pressed
    P1OUT &= ~(RED+GREEN);				// LEDs initially off
	playLight = 1;						// sets flag so that first number in recordMemory turns the LED on

    _bis_SR_register(GIE+LPM0_bits);	// after this instruction, the CPU is off!
}

interrupt void WDT_interval_handler(){

	if(P1IN&BUTTON){								// when button is not pressed
   		if(currentMode == 0){							// in recording mode
   			P1OUT = (P1OUT & (~GREEN)) | RED;				// only red LED is on (to show that device is recording)
   			if(lastButtonState == 1){						// if button was pressed at last interrupt check
   				lastButtonState = 0;							// note that button is no longer being pressed
   			   	recordCounter++;								// begin fresh counter for how long button isn't pressed
   			}
   			recordMemory[recordCounter]++;					// increase the count on how long the button hasn't been pressed / LED off
   			if(recordMemory[recordCounter] >= 300){			// if button hasn't been pressed for a few seconds
   				P1OUT &= ~(RED+GREEN);							// turn off LEDS
   				currentMode = 1;								// move into playback mode
   				recordCounter = 0;								// move back to beginning of sequence for playback
   			}
   		}
   		else if((currentMode == 1)&&(recordCounter != -1)){	// in playback mode
   			if(recordMemory[recordCounter] == 0){				// if current cell is empty
   				recordCounter++;									// moves onto next array element
   				playLight ^= 1;										// toggles to LED on or LED off mode (depending on previous mode)
   			}
   			else{
				if(playLight == 1){								// if in LED on mode
					P1OUT = (P1OUT & (~RED)) | GREEN;				// turn green LED on
					recordMemory[recordCounter]--;					// decrease counter on how long LED has to be on
				}
				else if(playLight == 0){						// if in LED off mode
					P1OUT &= ~(RED+GREEN);							// both lights off
					recordMemory[recordCounter]--;								// decrease count on how long LED has to stay off (a pause)
				}
   			}
   		}
	}
   	else{										// when button is pressed
   		if(currentMode == 0){						// in recording mode
   			P1OUT |= (RED+GREEN);						// both green LED and red LED are on
   	   		if(lastButtonState == 0){					// if button wasn't pressed at last interrupt check
   	   			lastButtonState = 1;						// note that button is being pressed
   	   			recordCounter++;							// begin fresh counter in next array cell for how long button stays pressed
   	   		}
   			recordMemory[recordCounter]++;					// increase the count on how long the button has been pressed / LED on
   		}
   		else if(currentMode == 1){					// in playback mode
   			if(recordCounter == -1){					// if waiting to go into record mode
   				currentMode = 0;							// goes into record mode
   				lastButtonState = 1;						// note that button is pressed
   				recordCounter = 0;							// leaves wait mode
   				recordMemory[recordCounter] = 1;				// records initial button press as first value in sequence
   			}
   			else{										// otherwise act as if button isn't pressed
   				if(recordMemory[recordCounter] == 0){		// if current cell is empty
   					recordCounter++;							// moves onto next array element
   					playLight ^= 1;								// toggles to LED on or LED off mode (depending on previous mode)
   				}
   				else{										// if sequence is being played back, ignores button presses and proceeds as if no button has been pressed
   					if(playLight == 1){							// if in LED on mode
   						P1OUT = (P1OUT & (~RED)) | GREEN;			// turn green LED on
   						recordMemory[recordCounter]--;				// decrease counter on how long LED has to be on
   					}
   					else if(playLight == 0){					// if in LED off mode
   						P1OUT &= ~(RED+GREEN);						// both lights off
   						recordMemory[recordCounter]--;				// decrease count on how long LED has to stay off (a pause)
   					}
   				}
   			}
		}
   	}

	if(recordCounter >= 35){		// when array limit reached
		if(currentMode == 0){			// in record mode, no more memory left for recording
			currentMode = 1;				// go into playback mode
			recordCounter = 0;				// and reset counter (move to beginning of sequence)
		}
		else if(currentMode == 1){		// in playback mode, no more values to play back
			recordCounter = -1;				// set flag to go idle until a button is pressed to signal record mode
			P1OUT &= ~(RED+GREEN);			// LEDs left off
			playLight = 1;					// resets flag so that first number in recordMemory turns the LED on
		}
	}


}

ISR_VECTOR(WDT_interval_handler,".int10")
