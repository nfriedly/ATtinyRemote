/*
 The fuses it set like so: -U lfuse:w:0xce:m -U hfuse:w:0xdc:m -U efuse:w:0xff:m
 This will set the ATtiny85 to use an external crystal at 8MHz or above
 The start-up time is set to 258 CK/14 CK + 4.1ms
 Brown-out detection is set to 4.3 and the SPI is enabled.
*/

//#define SLEEP // Uncomment this to save power, this will add a small delay after wake up, so fast presses wouldn't be decoded
#ifdef SLEEP
#include <avr/sleep.h> // Sleep mode is used between commands to save power
#endif

#define LED    PINB0 // pin 5 on ATtiny85
#define IRLED  PINB1 // pin 6 on ATtiny85
#define IRRECV PINB2 // pin 7 on ATtiny85

#define PWMFREQUENCY  38 // Frequency in kHz

/* The JVC Protocol is only 16-bits */
#define JVCPower        0xC5E8
#define JVCMute         0xC538
#define JVCVolumeDown   0xC5F8
#define JVCVolumeUp     0xC578

#define JVC_HDR_MARK    8000
#define JVC_HDR_SPACE   4000
#define JVC_BIT_MARK    600
#define JVC_ONE_SPACE   1600
#define JVC_ZERO_SPACE  550

// The panasonic protocol is 48-bits long, all the commands has the same address (or pre data) and then followed by a 32 bit code
#define PanasonicAddress      0x4004     // Panasonic address (Pre data)
#define PanasonicPower        0x100BCBD  // Panasonic Power button
#define PanasonicPower2       0x1000E0F  // Red button - alternative power button
#define PanasonicMute         0x1008E8F  // Green button
#define PanasonicVolumeDown   0x1004E4F  // Yellow
#define PanasonicVolumeUp     0x100CECF  // Blue button
#define ATtinyPower           0x190CB5A  // Second power button

#define PANASONIC_BITS  48 // The Panasonic Protol is 48 bits long

#define COMPAREFREQUENCY (1000/6) // Frequency value equal to a compare match every 6ms - don't remove the parentheses or it won't compile correctly
#define TIMERVALUE F_CPU/COMPAREFREQUENCY/1024-1 // See the datasheet page 75 - http://www.atmel.com/Images/doc2586.pdf - the equation is for a period, so don't divide by 2

volatile bool compareMatch = 0; // Used to indicate that a compare match has occurred
volatile uint16_t compareMatchCounter = 0; // Counter for every timer1 compare match

/*
 // pulse parameters in usec
 #define PANASONIC_HDR_MARK 3502
 #define PANASONIC_HDR_SPACE 1750
 #define PANASONIC_BIT_MARK 502
 #define PANASONIC_ONE_SPACE 1244
 #define PANASONIC_ZERO_SPACE 400
*/

#define headerTime  (TIMERVALUE*4000/6000) // 4000us
#define spaceTime   (TIMERVALUE*1324/6000) // (902+1746)/2=1324us

uint8_t IRState = 0;
uint8_t currentPulse = 0;
volatile uint64_t IRData = 0;
volatile bool finishedReading = 0;

bool deactivated = 0; // Used to disable the ATtinyRemote

void setup() {
  cli(); // Disables interrupts by setting the global interrupt mask

  /* Set up I/O's */
  DDRB |= _BV(LED); // Set as output
  DDRB |= _BV(IRLED); // Set as output
  PORTB &= ~(_BV(IRLED)); // When not sending PWM, we want it low
  DDRB &= ~(_BV(IRRECV)); // Set as input

  /* Set up timer0 to "Clear Timer on Compare Match (CTC) Mode" at a 6ms interval */
  TCCR0A = _BV(WGM01); // Set "Clear Timer on Compare Match (CTC) Mode" - OCR0A is TOP
  TCCR0B = _BV(CS02)| _BV(CS00); // Set precscaler to 1024
  OCR0A = TIMERVALUE; // Set the value, so there is a compare match every 6ms
  TIMSK = _BV(OCIE0A); //  Timer/Counter0 Output Compare Match A Interrupt Enable   

  /* Set up external interrupt on pin 7 */
  MCUCR = _BV(ISC01); // The falling edge of INT0 generates an interrupt request
  MCUCR &= ~(_BV(ISC00));
  GIMSK = _BV(INT0); // External Interrupt Request 0 Enable

  /* Enable PWM with a frequency of 38kHz on pin 6 (OC1A) */
  TCCR1 = _BV(PWM1A) | _BV(CS12); // Enable PWM and set prescaler to 8     
  OCR1C = (((F_CPU/8/PWMFREQUENCY/1000))-1); // Set PWM Frequency to 38kHz, OCR1C is TOP
  OCR1A = OCR1C/3; // 33% duty cycle

  sei(); // Enables interrupts by setting the global interrupt mask

  /* Indicate startup */
  PORTB |= _BV(LED); // Turn LED on
  newDelay(100);
  PORTB &= ~(_BV(LED)); // Turn LED off
}

void loop() {
  if(finishedReading) {
    if((uint16_t)(IRData >> 32) == PanasonicAddress) {   
      if(deactivated) {
        if((uint32_t)IRData == ATtinyPower) {
          deactivated = false;
          PORTB &= ~(_BV(LED)); // Turn LED off
          newDelay(250); // delay insures that it's just don't toggle "deactivate" very fast
          IRData = 0; // Reset data
          finishedReading = 0; // Clear flag          
        }
      }
      else {
        switch((uint32_t)IRData) {      
        case PanasonicVolumeUp:   
          JVCCommand(JVCVolumeUp);        
          break;
        case PanasonicVolumeDown:    
          JVCCommand(JVCVolumeDown);  
          break;
        case PanasonicMute:
          JVCCommand(JVCMute);
          break;
        case PanasonicPower:
          JVCCommand(JVCPower);
          delay(3000); // On Panasonic TVs one have to hold the power button to turn the TV on, this delay keeps the ATtinyRemote from toggling the JVC stereo on and off
          break;
        case PanasonicPower2:
          JVCCommand(JVCPower);
          break;
        case ATtinyPower: // This will disable the ATtinyRemote until the button is pressed again
          deactivated = true;
          PORTB |= _BV(LED); // Turn LED on
          newDelay(250); // delay insures that it's just don't toggle "deactivate" very fast          
          IRData = 0; // Reset data
          finishedReading = 0; // Clear flag
          break;
        default:
          break;     
        }
      }       
#ifdef SLEEP
      sleep(); // Put the device into sleep mode - the IR Receiver will trigger a external interrupt and wake the device
#endif
    } 
    else {
      IRData = 0; // Reset data
      finishedReading = 0; // Clear flag 
    }
  }  
}

void newDelay(uint16_t time) { // I use my own simple delay, as I can't use delay() as it uses timer0 - delayMicroseconds() still work, as it uses assembly language, see https://github.com/arduino/Arduino/blob/master/hardware/arduino/cores/arduino/wiring.c
  compareMatchCounter = time/6; // Set number of counts - The input is in ms and the resolution is 6ms
  TCNT0 = 0; // Reset timer 
  while(compareMatchCounter);
}

void JVCCommand(uint16_t data) { 
  /* Don't wait until finished sending command before decoding next incomming data */
  IRData = 0; // Reset data
  finishedReading = 0; // Clear flag

  /* Send the command */
  uint8_t nrRepeats;    
  if(data == JVCVolumeUp || data == JVCVolumeDown) // Only send one repeat if it's a volume up or down command
    nrRepeats = 1;
  else
    nrRepeats = 5;

  sendJVC(data,0); // hex value, no repeat
  for(uint8_t i = 0; i < nrRepeats; i++) {    
    delayMicroseconds(50);
    sendJVC(data,1); // hex value, repeat        
  }
}
void sendJVC(uint32_t data, uint8_t repeat) {
  if (!repeat){
    mark(JVC_HDR_MARK);
    space(JVC_HDR_SPACE); 
  }
  for (int i = 0; i < 16; i++) {
    if (data & 0x8000) { // The data is send MSB
      mark(JVC_BIT_MARK);
      space(JVC_ONE_SPACE); 
    } 
    else {
      mark(JVC_BIT_MARK);
      space(JVC_ZERO_SPACE); 
    }
    data <<= 1;
  }
  mark(JVC_BIT_MARK);
  space(0); // Turn IR LED off    
}
void mark(uint16_t time) {
  // Sends an IR mark for the specified number of microseconds.
  // The mark output is modulated at the PWM frequency.

  // OC1x cleared on compare match. Set whenTCNT1 = $00.
  // OC1x set on compare match. Cleared when TCNT1 = $00.    
  TCCR1 |= _BV(COM1A1); // Enable pin 6 (PB1) PWM output
  delayMicroseconds(time);    
}
void space(uint16_t time) {
  // Sends an IR space for the specified number of microseconds.
  // A space is no output, so the PWM output is disabled.

  // OC1x not connected.
  // OC1x not connected.
  TCCR1 &= ~(_BV(COM1A1)); // Disable pin 6 (PB1) PWM output  
  delayMicroseconds(time);
}

ISR(INT0_vect) { // External interrupt at INT0  
  if(!finishedReading) { // Wait until the data has been read
    switch(IRState) {
    case 0:
      if(compareMatch) // First pulse after long pause
        IRState = 1;
      break;
    case 1:
      if(!compareMatch && TCNT0 > headerTime) { // Check if the pulse is not to long and it is longer than the headerTime
        currentPulse = 0;
        IRState = 2;
      } 
      else
        IRState = 0;
      break;
    case 2:
      if(compareMatch) // To long a pause, so it must be a gap
        IRState = 0;
      else {
        if(TCNT0 < spaceTime) // Check if it's a mark
          IRData <<= 1;       
        else // It must be a space
        IRData = (IRData << 1) | 1;
        currentPulse += 1;          
        if (currentPulse == PANASONIC_BITS) { // All bits have been received
          finishedReading = 1; // Indicate that the reading is finished, this has to be cleared in code
          IRState = 0;
        }                
      }
      break;
    }
  }
  TCNT0 = 0; // Clear timer   
  compareMatch = 0; // Clear flag
}
ISR(TIM0_COMPA_vect) { // Timer0/Counter Compare Match A
  //PORTB ^= _BV(LED); // Used to check the timing with my oscilloscope
  compareMatch = 1; // It has been more than 6ms since last pulse were received
  compareMatchCounter--; // Used for newDelay()
}
#ifdef SLEEP
void sleep() { // The ATtiny85 is woken by a external interrupt on INT0 from the IR REceiver
  //PORTB &= ~(_BV(LED)); // Turn off LED when it goes to sleep

  // The falling edge of INT0 generates an interrupt request. 
  MCUCR &= ~(_BV(ISC00) | _BV(ISC01)); // To wake up from Power-down, only level interrupt for INT0 can be used. 

  set_sleep_mode(SLEEP_MODE_PWR_DOWN); // Set sleep mode
  sleep_mode(); // Here the device is put to sleep  
  
  // Disable level interrupt and set back to falling edge interrupt
  MCUCR = _BV(ISC01); // The falling edge of INT0 generates an interrupt request
  MCUCR &= ~(_BV(ISC00));
    
  TCNT0 = 0; // Clear timer
  compareMatch = 0; // Clear flag

  //PORTB |= _BV(LED); // Turn it back on
}
#endif

