#include <Arduino.h>

/*
 * This software is provided as-is and is only ment for demonstration purposes of the
 * AdamONE and RelayONE PCBs.
 * 
 * The serial port code started out as code from YouTube Channel GrettScott and has changed for
 * for our needs.
 * 
 * Use serial monitor or the HC05 BLE module plugged into connector. At Adam Electronics we use 
 * Bluetooth Terminal HC-05 Pro on a Google Pixel 2. This has additionally been tested using a HM10
 * bluetooth module, this is requried if you use an iPhone. The HC-05 will not work with iPhone.
 */

/* for timing */
unsigned long       currentMillis  = 0;;
unsigned long       previousMillis = 0;
const unsigned int  interval       = 1; /* 1 mSec tick */

/* event timers, random starting values so all events do not fire at same time during start-up */
unsigned int blink_tmr      = 90;
unsigned int serial_tmr     = 100;
unsigned int ad_tmr_rly     = 50;
unsigned int ad_tmr_pot1    = 25;

/* used for the heartbeat led1 */
#define      ERR_BLINK_RATE    100    /* if the relay voltage is not present, blink at a 100mSec rate */
#define      NO_ERR_BLINK_RATE 1000   /* if the relay voltage is present, blink at a 1 second rate */
unsigned int blink_rate        = NO_ERR_BLINK_RATE;
boolean      toggle            = 0;   /* for heartbeat LED1 */

/* differant leds on the AdamONE */
#define LED1 13
#define LED2 10
#define LED3 11 

/* for serial port */
String inputString    = "";       /* String to hold incoming data */
bool   stringComplete = false;    /* is the string complete? is it? we never know.... */
char   inChar         = 0;        /* for receiving incoming ascii charicter */

/* for relay shield */
#define RLY1                       4
#define RLY2                       7
#define RLY_DIAG                   A2
#define RLY_VOLTAGE_THRESHOLD      628  /* set for 10V at this time */
unsigned int  relay_voltage        = 0;


/* for testing out the potentiometer */
#define POT1 A5
unsigned int pot_voltage_raw = 0;
unsigned int flt_pot1_voltage = 0;

/* 
 *  we are implementing a first order lag filter for the potentiometer on A5, here is the floating point
 *  implementation of what we are trying to do.
 *
 *  new_filtered_value = k * raw_sensor_value + (1 - k) * old_filtered_value
 *  we are precalculated for .2 with 2 decimals of precision
 */
unsigned int scaled_filtered_value = 0;
unsigned int k_scaled              = 1;  /* k_scaled is k times 2 to the power of shift_amount, rounded to the next largest integer */
unsigned int preCompute            = 3;  /* The expression "((1 << shift_amount) - k_scaled)" can, of course, be precomputed. */
unsigned int shift_amount          = 2;  /* shift_amount is the number of bits past the binary point. remember preCompute! */

/* using this for good house keeping, clean buffers when done using them. */
void clearString()
{
  inputString = "";
}

/* put your setup code here, to run once: */
void setup() {
  /* 
   * these are the availible LEDs on the AdamONE. All are buffered through an op-amp 
   * so it is not requried to only use these as LEDs. They will still blink but you can 
   * use the pins for anything you want.
   */
  pinMode(LED1, OUTPUT); /* This is the heartbeat, something is wrong if this stops */
  pinMode(LED2, OUTPUT); /* This will turn on when Relay 1 is enabled */
  pinMode(LED3, OUTPUT); /* This will turn on when Relay 2 is enabled */
  

  /*  
   *  these pins will control the relays on the RelayONE 
   */
  pinMode(RLY1, OUTPUT);
  pinMode(RLY2, OUTPUT);

  /*
   * This is needed for the UART, used with Bluetooth HC-05 or Serial Monitor
   */
  Serial.begin(9600);
  Serial.println("Setup Complete");
}

/* put your main code here, to run repeatedly: */
void loop()
{
  /* 
   * crude way to have a 1 mSec time for events. Remember, the arduino platform become much more 
   * powerful when you do not wait for things. Many examples are written this way and is not a good way
   * to get full use of the system. Build the habit of checking and moving on to other things. 
   */
  currentMillis = millis();
  if ( currentMillis - previousMillis >= interval )
  {
    /* need to get a new value to evaluate on the next loop */
    previousMillis = currentMillis;

    /* decrement all timers */
    blink_tmr--;
    serial_tmr--;
    ad_tmr_rly--;
    ad_tmr_pot1--;

    /* 
     * this keeps things simple. we are simply decrementing timers here, then in the rest of loop
     * we check if anything we just decremented is now zero. if it is zero, then we run the code
     * and when finsihed up reload the timer again. 
     */
  }

  /*
   * this is the built in LED, just toggling it so we know that
   * software is running
   */
  if (blink_tmr == 0)
  {
    blink_tmr = blink_rate;
    toggle = !toggle;
    digitalWrite(LED1, toggle);
  }


  /*
   * here we will read A2 and make sure we have a voltage > 10V applied 
   * to the RelayONE. Without this, the relays will not operate as they 
   * have a coil voltage of 12V
   * 
   * This will not disable the indicator LEDs or relays but the blink rate will change
   * on LED1 to let you know of the issue. 
   */
  if(ad_tmr_rly == 0)
  {
    /* read the a/d voltage */
    relay_voltage = analogRead(RLY_DIAG);

    if(relay_voltage >= RLY_VOLTAGE_THRESHOLD)
    {
      blink_rate = NO_ERR_BLINK_RATE;
    }
    else
    {
      blink_rate = ERR_BLINK_RATE;
    }

    ad_tmr_rly = 150;
  }

  /*
   * here we will read A5 to get the voltage level on the potentiometer POT1. For giggles
   * I have placed the raw value through a first order lag filter I have used in the past. 
   * These work out nice if you want to take out the jitter from an a/d reading.
   */
  if(ad_tmr_pot1 == 0)
  {
    /* read the a/d voltage */
    pot_voltage_raw = analogRead(POT1);

    /* apply first order lag to pedal position */
    scaled_filtered_value = k_scaled * pot_voltage_raw + ( ( preCompute * scaled_filtered_value ) >> shift_amount );
    flt_pot1_voltage = scaled_filtered_value >> shift_amount;

    ad_tmr_pot1 = 50;
  }
   

  /*  
   * this is where we check and see if we have data from the 
   * serial port or ble module and process the commands.
   * 
   * There are probably more elegent ways to handle the commands, but this
   * serves the purpose. 
   * 
   * When your command is a single letter, be sure to use 'c' and not "c". It will
   * not work. Only use double quotes for more then 1 char "like this". This will 
   * save you two cups of coffee.
   */ 
  if(serial_tmr == 0)
  { 
    if (stringComplete = true)
    {
      if(inputString[0] == ('c') || inputString[0] == ('C') )
      {
        Serial.println("www.adamelectronics.com");
        Serial.println("dlarente@adamelectronics.net");
        Serial.println(" ");
        clearString();
        stringComplete = false;      
      }
      else if(inputString[0] == 'r') /* we will use r for relay commands */
      {
        switch(inputString[1])
        {
          case '1':
            if(inputString[2] == '0')
            {
              digitalWrite(RLY1, 0);
              digitalWrite(LED2, 0);
              Serial.println("relay 1 off");
              Serial.println(" ");
            }
            else if(inputString[2] == '1')
            {
              digitalWrite(RLY1, 1);
              digitalWrite(LED2, 1);
              Serial.println("relay 1 on");
              Serial.println(" ");
            }
            else
            {
              Serial.println("relay 1 command error");
              Serial.println(" ");
            }
          break;
          case '2':
            if(inputString[2] == '0')
            {
              digitalWrite(RLY2, 0);
              digitalWrite(LED3, 0);
              Serial.println("relay 2 off");
              Serial.println(" ");
            }
            else if(inputString[2] == '1')
            {
              digitalWrite(RLY2, 1);
              digitalWrite(LED3, 1);
              Serial.println("relay 2 on");
              Serial.println(" ");
            }
            else
            {
              Serial.println("relay 2 command error");
            }            
          break;
        }

        clearString();
        stringComplete = false; 
      }
      else if(inputString[0] == ('m') ||  inputString[0] == ('M') )
      {
        Serial.println("r10 = relay 1 off");
        Serial.println("r11 = relay 1 on");
        Serial.println("r20 = relay 2 off");
        Serial.println("r21 = relay 2 on");
        Serial.println("c = tech support");
        Serial.println("a = analog voltage of A2 and A5");
        Serial.println(" ");
        clearString();
        stringComplete = false;        
      }
      else if(inputString[0] == 'a')
      {
        Serial.print("A2, Relay Shield Voltage = ");Serial.println(relay_voltage);
        Serial.print("A5, Potentiometer Voltage = ");Serial.println(pot_voltage_raw);
        clearString();
        stringComplete = false; 
      }
      else /* keep this at the end */
      {
        clearString();
        stringComplete = false; 
      }
      serial_tmr = 50;
    }
  }
}
/*
 * SerialEvent occurs whenever a new data comes in the hardware serial RX. This
 * routine is run after each loop(), so do not use delays. get in and get out. 
 * Multiple bytes of data may be available.
 */
void serialEvent() 
{
  while( Serial.available() ) 
  {
    /*
     * if the incoming character is a newline, set a flag so the main loop can
     * do something with it. We do not want the new line in our buffer.
     */
    inChar = (char)Serial.read();
     
    if(inChar == '\n') 
    {
      stringComplete = true;
      inChar = 0;
    }
    else
    {
      stringComplete = false;
      inputString += inChar;
      inChar = 0;
    }
  }
}
