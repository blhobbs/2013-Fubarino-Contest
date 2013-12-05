////////////////////////////////////////////////////////////////
// Rechargeable Battery Capacity Tester
//     Tests up to three AA batteries simultaneously and
//     displays the results on a Nokia style LCD display
//     using the PCD8544 library
// Dec 5, 2013 Brian H

///////////////////////////////////////////////////////////////

#include <PCD8544.h>        // library of functions for Nokia LCD (http://code.google.com/p/pcd8544/)
#include <avr/pgmspace.h>   // allows use of PROGMEM data
#include "HackADayLogos.h"


#define LCD_TEXT_WIDTH      14   // Width of text line for Nokia LCD
#define LCD_LINE1            0
#define LCD_LINE2            1
#define LCD_LINE3            2
#define LCD_LINE4            3
#define LCD_LINE5            4
#define LCD_LINE6            5
#define NUM_LINES_ON_LCD     6
#define MAX_BATTERIES        3
#define LOAD_RESISTANCE      2.5    //load resistance in ohms
#define START_BEEP        5000      // frequency in Hz
#define DONE_BEEP         1800
#define SPKR_PIN             8     // The pin used for the SPEAKER
#define LED_PIN             13     // The pin used for the LED
#define LCD_WIDTH           84     // The dimensions of the Nokia LCD(in pixels)...
#define LCD_HEIGHT          48
#define BAT_WIDTH           18     // Width of battery icon in pixels
#define BAT_HEIGHT           1     // Height of battery Icon (1 line = 8 pixels)
#define BATTERY_ICON_HORIZ  34     // Horizontal position of battery icon (in pixels)
#define MAH_HORIZ_POSITION  60     // Horizontal position of mAh display

// Bitmaps for battery icons, Full, Empty, and Battery with an X (no battery)
static const byte batteryEmptyIcon[] ={ 0x1c, 0x14, 0x77,0x41,0x41,0x41,
           0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x7f};
static const byte batteryFullIcon[] = { 0x1c, 0x14, 0x77,0x7f,0x7f,0x7f,
           0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f};
static const byte noBatteryIcon [] = { 0x1C, 0x14, 0x77, 0x41, 0x41,0x41,
           0x41,0x63,0x77,0x5D,0x5D,0x77,0x63,0x41,0x41,0x41,0x41,0x7f};
///////// Battery voltage limits //////////////////////////////////////////
//  The following constants define the expected voltages for detecting the
//  battery and the minimum voltage at which the discharge test is complete.
// *Lithium Ion cells have a fully charged no-load voltage of 3.6 to 3.7 volts
//    and cause too much power dissapation in the load resistor, so 
//    they will not be tested
// *NiCads have a fully charged no-load voltage of 1.2 to 1.35 volts 
// *NiMH batteries have a fully charged no-load voltage of 1.4 to 1.45 volts
//    Since the NiCad and NiMH batteries are similar (and difficult to 
//    reliably autodetect) they are handled identically in this program.
////////////////////////////////////////////////////////////////////////////
#define MAX_VOLTAGE           1700  // Max Voltage used for detection (in mV)
#define NIMH_MIN_VOLTAGE      950   // niMH/NiCd Min Voltage for load removal (in mV)

static PCD8544 lcd;
enum {NOT_INSTALLED, DETECTING_TYPE, OVER_VOLTAGE, TEST_IN_PROGRESS, DONE};  // battery status values

struct batteryStruct
{
    unsigned long  charge;         // Total microamp hours for this battery
    byte battStatus;               // set this to DONE when this cell's test is complete
    byte batteryVoltagePin;        // Analog sensor pin (0-5) for reading battery voltage
    byte fetVoltagePin;            // Analog sensor pin (0-5) to read voltage across FET
    byte dischargeControlPin;      // Output Pin that controlls the load for this battery
    unsigned int lowerThreshold;   // voltage at which discharge is complete (mV)
    unsigned long PrevTime;        // Previous time reading (in milliseconds)
    unsigned int numSamplesAboveMin; // number of good voltage readings (to determine battery installed)
    unsigned int numSamplesBelowMin; // number of samples read below minimum (to determine battery discharged)
    
}battery[MAX_BATTERIES];




static const byte progressIndicator[] = "-\0|/";
static const byte backslashNokia[] =    //define backslach LCD character for Nokia5110 (PCD8544)
{
   B0000010,
   B0000100,
   B0001000,
   B0010000,
   B0100000
};

unsigned char logo_buffer [(LCD_WIDTH * LCD_HEIGHT) / 8];
const prog_uchar *FrameArray[]  =
        {HackADay9, HackADay8,HackADay7,HackADay6,HackADay5,
         HackADay4, HackADay3,HackADay2,HackADay1};


//Prototypes for utility functions
void printRightJustifiedUint(unsigned int n, unsigned short numDigits);
void ClearDisplayLine(int line);   // function to clear specified line of LCD display
unsigned int getBatteryVoltage(unsigned int batteryNum);
unsigned int getFetVoltage(unsigned int batteryNum);
void printVoltage(unsigned int n);
void DisplayLogo();
//-------------------------------------------


void setup()
{
   unsigned int batteryNum;
   unsigned int battVoltage;
  
   Serial.begin(9600);             // Initialize serial port
   pinMode(SPKR_PIN, OUTPUT);      //Set output mode for pin used for spkr
   
   lcd.begin(LCD_WIDTH, LCD_HEIGHT);// set up the LCD's dimensions in pixels
   // Register the backslash character as a special character
   // since the standard library doesn't have one
   lcd.createChar(0, backslashNokia);
   tone(SPKR_PIN, START_BEEP,50);   // short beep
   
   battery[0].dischargeControlPin = 13;    // setup the corresponding pins
   battery[0].fetVoltagePin = 0;           // for each battery according to
   battery[0].batteryVoltagePin = 1;       // schematic wiring diagram.
   
   battery[1].dischargeControlPin = 12;       
   battery[1].fetVoltagePin = 2; 
   battery[1].batteryVoltagePin = 3; 

   battery[2].dischargeControlPin = 11;
   battery[2].fetVoltagePin = 4; 
   battery[2].batteryVoltagePin = 5; 
  
   battery[0].battStatus = DETECTING_TYPE;  // initialize status of each battery
   battery[1].battStatus = DETECTING_TYPE; 
   battery[2].battStatus = DETECTING_TYPE;
  
   battery[0].numSamplesAboveMin = 0;
   battery[1].numSamplesAboveMin = 0;
   battery[2].numSamplesAboveMin = 0;
  
   battery[0].numSamplesBelowMin = 0;
   battery[1].numSamplesBelowMin = 0;
   battery[2].numSamplesBelowMin = 0;  

   // Set the three FET control pins for output
   pinMode(battery[0].dischargeControlPin, OUTPUT); 
   pinMode(battery[1].dischargeControlPin, OUTPUT); 
   pinMode(battery[2].dischargeControlPin, OUTPUT); 

   lcd.setCursor(0, LCD_LINE1);// First Character, First line
   lcd.print(" Rechargeable");  // Print a message to the LCD.
   lcd.setCursor(0, LCD_LINE3);
   lcd.print("   Battery    "); 
   lcd.setCursor(0, LCD_LINE5);   
   lcd.print("    Tester    "); 
 
   delay(3000);

   lcd.clear();        // clear the display 
   lcd.print(" ** Testing * "); 
   lcd.setCursor(0, LCD_LINE3);//  set cursor to 3rd line 
   lcd.print("Volts      mAh");  
   
}

void loop() 
{
   static unsigned int i = 0 ;
   static unsigned int  line , p, batteryNum, width1;
   static unsigned long duration, currentTime;
   static unsigned int battVoltage,fetVoltage, loadCurrent;
   static boolean done = false;
   static unsigned int beepCounter = 0;

   if (!done)
   {
      lcd.setCursor(LCD_WIDTH-5, LCD_LINE1);  // end of line 1
      lcd.write(progressIndicator[i  % (sizeof(progressIndicator)-1)]);
      lcd.home();
      lcd.write(progressIndicator[i++ % (sizeof(progressIndicator)-1)]);
      
      for (batteryNum= 0 ; batteryNum < MAX_BATTERIES ; batteryNum++)
      {
         battVoltage = getBatteryVoltage(batteryNum);
         fetVoltage = getFetVoltage(batteryNum);
         // Calculate the display line number for this battery
         line = batteryNum + LCD_LINE4;  // first battery displayed on line4
            
         if ( battery[batteryNum].battStatus == TEST_IN_PROGRESS)
         {
            ClearDisplayLine(line);
            printVoltage(battVoltage);
            lcd.setCursor(BATTERY_ICON_HORIZ , line);   // indent to horiz pixel location for battery icon     
            width1 =  3 + (i % (sizeof(batteryEmptyIcon)-3)) ; //start at offset of 3 pixels
  
            // Display the left half of the Battery Icon (Empty), at calculated width
            lcd.drawBitmap(batteryEmptyIcon, width1, 1); // Battery Empty icon (partial) one line tall
            // Display the remainder of the Battery Icon (Full) 
            lcd.drawBitmap(&batteryFullIcon[width1], sizeof(batteryFullIcon) - width1, 1);

            // Calculate the time duration between the last reading (in milliseconds)
            currentTime = millis();
            duration = (currentTime - battery[batteryNum].PrevTime);
            battery[batteryNum].PrevTime = currentTime;
            // Current through resistor is voltage across the resistor divided by load restistance
            // Since the voltage is in millivolts, the current will be in milliamps
            loadCurrent = (battVoltage - fetVoltage) / LOAD_RESISTANCE;
            // milliAmpHours = current (in milliAmps)  * duration (in Hours)
            // Must divide by (60*60*1000) to convert duration in micro seconds to hours
            // But doing this now would cause a loss of precision, so
            // divide by 3600 which will result in microamp hours to be summed.
            // Divide by 1000 when milliamp hours are desired for display
            battery[batteryNum].charge +=   (loadCurrent * duration) / 3600;
            Serial.print("Bat");
            Serial.print(batteryNum+1);
            Serial.print("  V=");
            Serial.print(battVoltage);
            Serial.print("   duration ");
            Serial.print(duration);
            Serial.print("ms   loadCurrent=");
            Serial.print(loadCurrent );
            Serial.print("mA     ChargeDrawn=");
            Serial.println(battery[batteryNum].charge/1000);
    
            lcd.setCursor(MAH_HORIZ_POSITION, line);  // indent to pixel location for mAh
            printRightJustifiedUint(battery[batteryNum].charge/1000,4);
            // Has the battery voltage dropped below the minimum?
            // Must have several battery voltage samples below minimum
            // in a row, before declaring 'done'
            if (battVoltage <  battery[batteryNum].lowerThreshold)
            {
               Serial.print("Batt");
               Serial.print(batteryNum);
               Serial.print(" Below threshold: ");
               Serial.println(battVoltage);
               if ( battery[batteryNum].numSamplesBelowMin < 3 )
               {
                  battery[batteryNum].numSamplesBelowMin++;
               }
               else
               {
                  // This testing on this battery is complete, set status to DONE
                  // Turn off the discharge load and update the display
                  battery[batteryNum].battStatus = DONE;
                  digitalWrite(battery[batteryNum].dischargeControlPin, LOW); // turn off the load
                  ClearDisplayLine(line);
                  // Display the Empty Battery Icon 
                  lcd.setCursor(BATTERY_ICON_HORIZ , line);   // indent to horiz pixel location for battery icon     
                  lcd.drawBitmap(batteryEmptyIcon, sizeof(batteryEmptyIcon), 1);
                  lcd.setCursor(MAH_HORIZ_POSITION, line);  // indent to pixel location for mAh
                  printRightJustifiedUint(battery[batteryNum].charge/1000,4); 
                  tone(SPKR_PIN, DONE_BEEP,50); // short beep
                  // Check to see if ALL installed batteries are in the DONE state.
                  done = checkAllDone();
               }
            }
            else
            {
              // reset counter since this reading was good.
             battery[batteryNum].numSamplesBelowMin = 0;
            } 
         }
         else if (battery[batteryNum].battStatus == DONE)
         {
            // don't update this line of the display    
         }
         else if (battery[batteryNum].battStatus == NOT_INSTALLED)
         {
           ClearDisplayLine(line);
           lcd.setCursor(BATTERY_ICON_HORIZ , line);   // indent to horiz pixel location for battery icon     
           lcd.drawBitmap(noBatteryIcon, sizeof(noBatteryIcon), 1);
           if (battVoltage >= NIMH_MIN_VOLTAGE)
           {
              // This condition indicates that a battery has 
              // been installed, change status to Detecting
              battery[batteryNum].battStatus = DETECTING_TYPE;
           }   
         }  
         else if (battery[batteryNum].battStatus == OVER_VOLTAGE )
         {
           lcd.setCursor(0, line);
           lcd.print(" OVER VOLTAGE ");
           battery[batteryNum].battStatus =  DETECTING_TYPE;
         } 
         else if (battery[batteryNum].battStatus == DETECTING_TYPE)
         {
           lcd.setCursor(0, line);
           lcd.print(" Detecting... ");
           detectBatteryType(batteryNum, battVoltage );
         }
         else // undefined battery status
         {
            // should never get here
            lcd.setCursor(0, line);
            lcd.print("???");
         }  
      }
   }
   else
   {
      // we're done - all batteries are discharged
      if (beepCounter  < 3 )
      {
         lcd.setInverse(beepCounter % 2); // Invert the display
         tone(SPKR_PIN, DONE_BEEP,200);  // done beep
         beepCounter++;
      }
      else if ( beepCounter == 3)
      {
         DisplayLogo();
         beepCounter++;
         delay(5000);
         lcd.clear();        // clear the display 
         return;
      }
      else
      {
         // Show results and continue to update battery voltage
         lcd.setCursor(0, LCD_LINE1);// First Character, First line
         lcd.print("     Test     ");
         lcd.setCursor(0, LCD_LINE2);// First Character, First line
         lcd.print("   Complete   ");
         lcd.setCursor(0, LCD_LINE3);//  set cursor to 3rd line 
         lcd.print("Volts      mAh");  
         for (batteryNum= 0 ; batteryNum < MAX_BATTERIES ; batteryNum++)
         {
            battVoltage = getBatteryVoltage(batteryNum);
            line = batteryNum + LCD_LINE4;  // first battery displayed on line4
            lcd.setCursor(0 , line);
            printVoltage(battVoltage);
            if (battery[batteryNum].battStatus == DONE)
            {
              lcd.setCursor(BATTERY_ICON_HORIZ , line);   // indent to horiz pixel location for battery icon     
               // Display the Battery Icon (Empty)
               lcd.drawBitmap(batteryEmptyIcon, sizeof(batteryEmptyIcon), 1); 
               lcd.setCursor(MAH_HORIZ_POSITION, line);  // indent to pixel location for mAh
               printRightJustifiedUint(battery[batteryNum].charge/1000,4);
            }
            else
            {
              //ClearDisplayLine(line);
              lcd.setCursor(BATTERY_ICON_HORIZ , line);   // indent to horiz pixel location for battery icon     
              lcd.drawBitmap(noBatteryIcon, sizeof(noBatteryIcon), 1);
            }
         }
      }
      
   }
    delay(1000);  // wait one second, then get next set of samples
} //end of main loop


//////////////////////////////////////////////////////////////////////////////////
// detectBatteryType()  Detects the battery type and sets up the appropriate
//               status and thresholds
//////////////////////////////////////////////////////////////////////////////////
void detectBatteryType(unsigned int batteryNum, unsigned int battVoltage)
{
   if (battery[batteryNum].numSamplesAboveMin > 3)
   {
      if (battVoltage > MAX_VOLTAGE )
      {
         battery[batteryNum].battStatus = OVER_VOLTAGE;
      }
      else if (battVoltage > NIMH_MIN_VOLTAGE)
      {
         // Battery Type identified as: NiMH or NiCd Battery
         // Initialize variables and start discharge
         battery[batteryNum].lowerThreshold = NIMH_MIN_VOLTAGE;
         battery[batteryNum].battStatus = TEST_IN_PROGRESS;
         battery[batteryNum].charge = 0;
         battery[batteryNum].PrevTime = millis();
         digitalWrite(battery[batteryNum].dischargeControlPin, HIGH); // turn on the FET
      }
      else
      {
         battery[batteryNum].numSamplesAboveMin = 0;
      }             
   }
   else // not enough good samples yet
   {
      if (battVoltage > NIMH_MIN_VOLTAGE)
      {
         battery[batteryNum].numSamplesAboveMin++;
         battery[batteryNum].numSamplesBelowMin = 0;
      }
      else
      {
         battery[batteryNum].numSamplesBelowMin++;
         battery[batteryNum].numSamplesAboveMin = 0;
      }
   }
   if (battery[batteryNum].numSamplesBelowMin > 3)
   {
      battery[batteryNum].battStatus = NOT_INSTALLED;
   }      
}


//////////////////////////////////////////////////////////////////////////////////
// checkAllDone()  checks to see if ALL installed batteries are in the DONE state.
//                 return true if all tests are complete.
//////////////////////////////////////////////////////////////////////////////////
boolean checkAllDone()
{
  unsigned int batteryNum;
  unsigned int count=0;
  
   for (batteryNum= 0 ; batteryNum < MAX_BATTERIES ; batteryNum++)
   {
     if( battery[batteryNum].battStatus == TEST_IN_PROGRESS)
        return false;
   }
   return true;
}      
      
//////////////////////////////////////////////////////////////////////////////////
// printRightJustifiedUint() prints unsigned integer, right justified
//            on the LCD with the specified number of digits (up to 5)
//            supressing leading zeros. Prints asterisks if the number is too
//            big to be displayed.
//////////////////////////////////////////////////////////////////////////////////
void printRightJustifiedUint(unsigned int n, unsigned short numDigits)
{
  
  const unsigned int powersOfTen[]={1,10,100,1000,10000};
  boolean overflow = false, supressZero = true;
  unsigned int digit, d;
  
  for (d = numDigits ; d > 0 ; d--)
  {
     if (overflow || numDigits > 5)
     {
       lcd.print("*");
     }
     else
     {
       // pow() function doesn't work as expected - use array powersOfTen[]
       digit = n / powersOfTen[d-1];
       n = n % (powersOfTen[d-1]);
       if (digit == 0 && supressZero && d > 1)
          lcd.print(" ");
       else if (digit <= 9)
       {
          lcd.print(digit);
          supressZero = false;
       }
       else
       {
         overflow = true;
         lcd.print("*");
       }
     }
  }          
}

//////////////////////////////////////////////////////////////////////////////////
// printVoltage() prints unsigned integer (millivolts), as a voltage with
//        decimal point on the LCD from 0.000 to 9.999 volts
//        Prints asterisks if the number is too big to be displayed.
//////////////////////////////////////////////////////////////////////////////////
void printVoltage(unsigned int n)
{
  
  const unsigned int powersOfTen[]={1,10,100,1000,10000},  numDigits = 4;
  boolean overflow = false, supressZero = true;
  unsigned int digit, d;
  
  for (d = numDigits ; d > 0 ; d--)
  {
     if (overflow)
     {
       lcd.print("*");
     }
     else
     {
       // pow() function doesn't work as expected - use array powersOfTen[]
       digit = n / powersOfTen[d-1];
       n = n % (powersOfTen[d-1]);
       if (digit <= 9)
       {
          lcd.print(digit);
          if (d == numDigits)
             lcd.print(".");
       }
       else
       {
         overflow = true;
         lcd.print("*");
       }
     }
  }          
}


//////////////////////////////////////////////////////////////////////////////////
// ClearDisplayLine()  utility function to clear one full line of the display
//////////////////////////////////////////////////////////////////////////////////
void ClearDisplayLine(int line) 
{
  unsigned int i;
  lcd.setCursor(0, line);  // put cursor on first char of specified line
  lcd.clearLine();
  lcd.home();
}

//////////////////////////////////////////////////////////////////////////////////
// Read analog input for specified battery and maps into a voltage (in millivolts)
//////////////////////////////////////////////////////////////////////////////////
unsigned int getBatteryVoltage(unsigned int batteryNum)
{
   //return analogRead(battery[batteryNum].batteryVoltagePin), *4.887;
   return map(analogRead(battery[batteryNum].batteryVoltagePin), 0,1023,0,5000);
}

//////////////////////////////////////////////////////////////////////////////////
// Read analog input for specified battery's FET and maps into a voltage (in millivolts)
//////////////////////////////////////////////////////////////////////////////////
unsigned int getFetVoltage(unsigned int batteryNum)
{
  //return analogRead(battery[batteryNum].fetVoltagePin)*4.887;
   return map(analogRead(battery[batteryNum].fetVoltagePin), 0,1023,0,5000);
}


//////////////////////////////////////////////////////////////////////////////////
// DisplayLogo()  Display bitmap logo on LCD 
//////////////////////////////////////////////////////////////////////////////////
void DisplayLogo() 
{
    int i, frame;
   lcd.clear();        // clear the display & set cursor to line0
    
    memset(logo_buffer, 0 , sizeof(logo_buffer));   // clear buffer for logo
    for ( frame = 0 ; frame < 9 ; frame++)
    {
     memcpy_P(logo_buffer, FrameArray[frame], sizeof(logo_buffer));
     lcd.drawBitmap(logo_buffer, LCD_WIDTH, LCD_HEIGHT/8); 
     delay(100);
    }
    
}

