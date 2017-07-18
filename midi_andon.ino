//http://playground.arduino.cc//Code/EEPROMWriteAnything
#include <EEPROM.h>

#define DEBUG

namespace ButtonState
{
    enum Enum : uint8_t
    {
      None = 0,
      Inc,
      Next,
      Mode,
      Hold  // after press and hold for b_mode
    };
}

namespace ProgramMode
{
    enum Enum : uint8_t
    {
      Init = 0,
      Demo,
      Normal,
      Edit
    };
}

byte initialbyte;
byte g_channel;
byte g_velocity;
byte g_midimode;
byte lamp;
byte value;
byte velocities[127] = {0};

volatile int g_nHoldCounter;
volatile ButtonState::Enum g_buttonState;
volatile ProgramMode::Enum g_programMode;

// Exponential PWM values
//static const byte s_fadeValues[]      = { 255, 180, 128,  90,  64,  45,  32,  23,  16,  12,   8,   6,   4,   3,   2,   1,  0 };

unsigned long time = 0;
int current_button_state = HIGH;
int button_counter = 0;
int debounce_count = 10;

#define ACTIVITYLED 13
#define PUSHBUTTON 52
#define RELAY 7
#define LAMP1 6
#define LAMP2 5
#define LAMP3 4
#define LAMP4 3
#define LAMP5 2

#define MIDI_START          0x80
#define MIDI_PROGRAM_CHANGE 0xB0
#define MIDI_MASK_OMNI      0xf0
#define MIDI_INVALID        0xFF
#define MIDI_MASK_ABOVE     0x80

////////////////////////////////////////////////////////////////////
// setup
////////////////////////////////////////////////////////////////////
void setup()  
{
  pinMode( ACTIVITYLED, OUTPUT );
  pinMode( PUSHBUTTON, INPUT_PULLUP );
  for ( byte nPin = LAMP5; nPin <= RELAY; nPin++ )
    pinMode( nPin, OUTPUT );

  // Assume program change
  g_midimode = MIDI_PROGRAM_CHANGE;

  g_nHoldCounter = 0;
  g_buttonState = ButtonState::None;
  g_programMode = ProgramMode::Demo; //M_NORMAL

  // Open serial communications and wait for port to open:
  Serial.begin( 115200 );
  //Serial1.begin( 31250 );
  Serial2.begin( 31250 );

  // wait for serial port to connect. Needed for Leonardo only
  while (!Serial);
  //while (!Serial1);
  while (!Serial2);
  Serial.println( "done.." );

  value = 0;
  lamp = 0;

  resetLamp();
}

////////////////////////////////////////////////////////////////////
// loop
////////////////////////////////////////////////////////////////////
void loop()
{
  // Wait a millisecond
  //delay( 1 ); ?
  while( millis() == time );
  time = millis();

  handleMidiMessage();
  handleButton();

  // Handle the current user mode
  switch ( g_programMode )
  {
    case ProgramMode::Init:
      // should not reach this..
      //doInitTick();
      break;

    case ProgramMode::Demo:
      // TODO: fancy demo
      doDemoTick();
      break;

    case ProgramMode::Normal:
      // Normal operation
      doNormalTick();
      break;
  
    case ProgramMode::Edit:
      // Configuration mode
      doConfigTick();
      break;
  }

  // Determine if it was press-and-hold
  if ( g_buttonState == ButtonState::Mode && g_nHoldCounter )
    g_buttonState = ButtonState::Hold;

  // or reset the button
  if ( g_buttonState != ButtonState::Hold )
    g_buttonState = ButtonState::None;
}

void resetLamp()
{
  for ( byte nPin = LAMP5; nPin <= RELAY; nPin++ )
    digitalWrite( nPin, LOW );
}

////////////////////////////////////////////////////////////////////
// handleMidiMessage
////////////////////////////////////////////////////////////////////
void handleMidiMessage()
{
  // handle midi signals
  
  // Always clear g_channel to make sure we only handle it once per tick
  g_channel = MIDI_INVALID;

  if( Serial2.available() > 0 )
  {
    digitalWrite( ACTIVITYLED, HIGH );
    initialbyte = Serial2.read(); 
#ifdef DEBUG
  Serial.write( "midi message: " );
  Serial.println( initialbyte, HEX ); 
#endif
    
    // Start of MIDI message? Assign mode
    if ( (initialbyte & MIDI_START) == MIDI_START )
    {
      g_midimode = initialbyte;
      // Clear g_channel byte
      //g_channel = MIDI_INVALID;
    }
    else
    {
      // No mode change, Copy over byte as channel
      g_channel = initialbyte;
    }
    
#ifdef DEBUG
    Serial.print( "midi cmd:" );
    Serial.println( g_midimode, HEX );
#endif
    // Mask out g_channel (omni-receive) and check which midi command we have
    switch ( g_midimode & MIDI_MASK_OMNI )
    {
      case MIDI_PROGRAM_CHANGE: // Program change (3 bytes)
#ifdef DEBUG
  Serial.write( "program change: " );
#endif

        // No g_channel set yet?
        if ( g_channel == MIDI_INVALID )
        {
          while( !Serial2.available() );
          g_channel = Serial2.read();
#ifdef DEBUG
  Serial.print( g_channel, HEX );
  Serial.print( " " );
#endif
        }

        while( !Serial2.available() );
        g_velocity = Serial2.read();
#ifdef DEBUG
  Serial.print( g_velocity, HEX );
#endif

        break;
    }
   
    digitalWrite( ACTIVITYLED, LOW );
  }
};

////////////////////////////////////////////////////////////////////
// handleButton
////////////////////////////////////////////////////////////////////
void handleButton()
{
  int reading = digitalRead( PUSHBUTTON );

  if ( reading == current_button_state && button_counter > 0 )
      button_counter--;
  
  if ( reading != current_button_state )
       button_counter++; 

  // If the Input has shown the same value for long enough let's switch it
  // handle the button
  if( button_counter >= debounce_count )
  {
    button_counter = 0;
    current_button_state = reading;
  }
  
  // Button pressed?
  if ( !current_button_state )
  {
    g_nHoldCounter++;

#ifdef DEBUG
    if ( g_nHoldCounter == 1 )
      Serial.println( "button pressed" );
#endif

    // Automatic config mode
    if ( g_nHoldCounter >= 1000 && g_buttonState != ButtonState::Hold )
    {
#ifdef DEBUG
      Serial.println( "button (active) mode" );
#endif
      g_buttonState = ButtonState::Mode;
    }
  }
  else if ( g_nHoldCounter )
  {

    if ( g_nHoldCounter >= 30 && g_nHoldCounter < 150 )
    {
#ifdef DEBUG
      Serial.println( "button released: pulse" );
#endif
      // Short 'pulse'
      g_buttonState = ButtonState::Inc;
    }
    else if ( g_nHoldCounter >= 150 && g_nHoldCounter < 1000 )
    { // 500
#ifdef DEBUG
      Serial.println( "button released: next" );
#endif
      // medium 'regular to 1 sec'
      g_buttonState = ButtonState::Next;
      // submit
    }
    else if ( g_nHoldCounter >= 1000 && g_buttonState == ButtonState::None )
    { // 1500
#ifdef DEBUG
      Serial.println( "button released: mode" );
#endif
      // long '1 sec to 3 sec'
      //config mode, only when button was not pressed
      g_buttonState = ButtonState::Mode;
    }
    else
    {
      g_buttonState = ButtonState::None;
    }

    g_nHoldCounter = 0;
  }
};


////////////////////////////////////////////////////////////////////
// doDemoTick
////////////////////////////////////////////////////////////////////
void doDemoTick()
{
  value++;

  switch ( g_buttonState )
  {
    case ButtonState::Mode:
      Serial.println( "switching to normal" );
      resetLamp();
      g_programMode = ProgramMode::Normal;
      break;

    case ButtonState::Inc:
    case ButtonState::Next:
      // Blink the blue led

      break;

    default:
      if ( value > 50 && value < 100 )
        digitalWrite( LAMP1, HIGH );
      else
        digitalWrite( LAMP1, LOW );

      if ( value > 100 && value < 150 )
        digitalWrite( LAMP2, HIGH );
      else
        digitalWrite( LAMP2, LOW );

      if ( value > 150 && value < 200 )
        digitalWrite( LAMP3, HIGH );
      else
        digitalWrite( LAMP3, LOW );

      if ( value > 200 && value < 250 )
        digitalWrite( LAMP4, HIGH );
      else
        digitalWrite( LAMP4, LOW );
  
      break;
  }

};

////////////////////////////////////////////////////////////////////
// doNormalTick
////////////////////////////////////////////////////////////////////
void doNormalTick()
{
  switch ( g_buttonState )
  {
    case ButtonState::Mode:
      Serial.println( "switching to edit" );
      resetLamp();
      g_programMode = ProgramMode::Edit;
      break;

    case ButtonState::Inc:
    case ButtonState::Next:
      // Blink the blue led
      break;

    default:
    
      //Serial.print( "normal tick" );

      // Channel info
      if ( g_channel == MIDI_INVALID )
        return;

      // Updated value?
      if ( velocities[ g_channel ] == g_velocity )
        return;

      velocities[ g_channel ] = g_velocity;
      
      // read the g_velocity flags from eeprom
      
      byte lights = determineLights( );
      Serial.print( "l:" );
      Serial.println( lights, BIN );

      // TODO: work out all lights and their modes
      digitalWrite( ACTIVITYLED, lights );

      for ( byte idx = 0; idx < 5; idx++ )
      {
        if ( lights & ( 1 << idx ) )
          digitalWrite( 6 - idx, lights & ( 1 << idx ) );
        else
          digitalWrite( 6 - idx, lights & ( 1 << idx ) );
      }

      // LAMP5?? Relay?
      digitalWrite( 2, lights & ( 1 << 3 ) );
  }
};

////////////////////////////////////////////////////////////////////
// doConfigTick
////////////////////////////////////////////////////////////////////
void doConfigTick()
{
  value++;

  switch ( g_buttonState )
  {
    case ButtonState::Mode:
      Serial.println( "switching to demo" );
      resetLamp();
      g_programMode = ProgramMode::Demo;
      break;

    case ButtonState::Inc:
      break;

    case ButtonState::Next:
      // Blink the blue led
      resetLamp();

      lamp++;
      if ( lamp >= 5 )
        lamp = 0;
      break;

    default:
      if ( value > 127 )
        digitalWrite( lamp + 3, HIGH );
      else
        digitalWrite( lamp + 3, LOW );

      break;
  }
};


byte determineLights( )
{
  byte lights = 0;
  byte currentLightBit = 0;
  int nextLightPos = 0;

  byte memVal;
  byte chan = MIDI_INVALID;
  byte vel;

  // Walk the memory and see if the g_channel is in there
  for ( int idx = 0; idx < 64; idx++ )
  {
    memVal = readMemory( idx );

    // increase the light
    if ( idx == nextLightPos )
    {
      
      // Increase the bit shifter for lamp flag
      currentLightBit++;

      // Get the address of the next lamp
      nextLightPos = memVal;
      chan = MIDI_INVALID;
      continue;
      
    }
   
    // read g_channel and value
    if ( chan == MIDI_INVALID )
    {
      chan = memVal;
      continue;
    }
    
    vel = memVal;
       
    // Got g_velocity: check values
    // Did the g_channel match?
    if ( (vel & MIDI_MASK_ABOVE) && velocities[ chan ] > ( vel & ( MIDI_MASK_ABOVE - 1 ) ) )
    {
      // g_velocity bigger, set bit
      lights |= (1 << (currentLightBit - 1) );
    }
    else if ( !(vel & MIDI_MASK_ABOVE) && velocities[ chan ] < vel )
    {
      // g_velocity smaller, set bit
      lights |= (1 << (currentLightBit - 1));
    }
    else {
      // no match, clear bit
      //lights &= ~(1 << currentLightBit);
    } // flag check

    // Reset g_channel, so we can read g_velocity      
    chan = MIDI_INVALID;
  } // for loop

  return lights;
};

////////////////////////////////////////////////////////////////////
// 
////////////////////////////////////////////////////////////////////
byte readMemory( short _nAddr )
{
  // TODO: implement o-buffer ROM
  //byte fakeMem[14] = { 7, 1, 10 | MIDI_MASK_ABOVE, 2, 15 | MIDI_MASK_ABOVE, 3, 100, 0, 4, 100 | MIDI_MASK_ABOVE, 5, 10 | MIDI_MASK_ABOVE, 6, 10 };

  // <absolute index of next color>, [<g_channel>, <threshold>, ...]
  // (| MIDI_MASK_ABOVE means above, else below)
  // blue, green, orange, red + relay
byte fakeMem[ ] = {
  9, 29, 10 | MIDI_MASK_ABOVE, // blue
     30, 10 | MIDI_MASK_ABOVE,
     31, 10 | MIDI_MASK_ABOVE,
     32, 10 | MIDI_MASK_ABOVE,
 12, 61, 10,                   // green
 15, 61, 10 | MIDI_MASK_ABOVE, // orange
 64,  1, 10 | MIDI_MASK_ABOVE, // red
      2, 10 | MIDI_MASK_ABOVE,
      3, 10 | MIDI_MASK_ABOVE,
      4, 10 | MIDI_MASK_ABOVE,
      5, 10 | MIDI_MASK_ABOVE,
      6, 10 | MIDI_MASK_ABOVE,
      7, 10 | MIDI_MASK_ABOVE,
      8, 10 | MIDI_MASK_ABOVE,
      9, 10 | MIDI_MASK_ABOVE,
     10, 10 | MIDI_MASK_ABOVE,
     11, 10 | MIDI_MASK_ABOVE,
     12, 10 | MIDI_MASK_ABOVE,
     // jack/cinch in
     17, 10 | MIDI_MASK_ABOVE,
     18, 10 | MIDI_MASK_ABOVE,
     19, 10 | MIDI_MASK_ABOVE,
     20, 10 | MIDI_MASK_ABOVE,
     21, 10 | MIDI_MASK_ABOVE,
     22, 10 | MIDI_MASK_ABOVE,
     23, 10 | MIDI_MASK_ABOVE,
     24, 10 | MIDI_MASK_ABOVE,
     25, 10 | MIDI_MASK_ABOVE,
     26, 10 | MIDI_MASK_ABOVE,
     27, 10 | MIDI_MASK_ABOVE,
     28, 10 | MIDI_MASK_ABOVE,
};
  /*
  byte fakeMem[ ] = {  3, 13, 10 | MIDI_MASK_ABOVE, // blue
                       6, 61, 10,                   // green
                      39,  1, 10 | MIDI_MASK_ABOVE, // orange
                           2, 10 | MIDI_MASK_ABOVE,
                           3, 10 | MIDI_MASK_ABOVE,
                           4, 10 | MIDI_MASK_ABOVE,
                           5, 10 | MIDI_MASK_ABOVE,
                           6, 10 | MIDI_MASK_ABOVE,
                           7, 10 | MIDI_MASK_ABOVE,
                           8, 10 | MIDI_MASK_ABOVE,
                           9, 10 | MIDI_MASK_ABOVE,
                          10, 10 | MIDI_MASK_ABOVE,
                          11, 10 | MIDI_MASK_ABOVE,
                          12, 10 | MIDI_MASK_ABOVE,
                          13, 10 | MIDI_MASK_ABOVE,
                          14, 10 | MIDI_MASK_ABOVE,
                          15, 10 | MIDI_MASK_ABOVE,
                          16, 10 | MIDI_MASK_ABOVE,
                       0, 61,  9 | MIDI_MASK_ABOVE, // red
                      };
                      */
  return fakeMem[ _nAddr ];
}

////////////////////////////////////////////////////////////////////
// 
////////////////////////////////////////////////////////////////////
byte writeMemory( short _nAddr, byte _nValue )
{
  return false;
}



// Old display stuff
//#include <LiquidCrystal.h>
/*
// Define fader images
//LiquidCrystal lcd( 6, 7, 8, 9, 10, 11 );
byte fader[8][8] = {
  0b00100,0b00100,0b00100,0b00100,0b00100,0b00100,0b00100,0b01110,
  0b00100,0b00100,0b00100,0b00100,0b00100,0b00100,0b01110,0b00100,
  0b00100,0b00100,0b00100,0b00100,0b00100,0b01110,0b00100,0b00100,
  0b00100,0b00100,0b00100,0b00100,0b01110,0b00100,0b00100,0b00100,
  0b00100,0b00100,0b00100,0b01110,0b00100,0b00100,0b00100,0b00100,
  0b00100,0b00100,0b01110,0b00100,0b00100,0b00100,0b00100,0b00100,
  0b00100,0b01110,0b00100,0b00100,0b00100,0b00100,0b00100,0b00100,
  0b01110,0b00100,0b00100,0b00100,0b00100,0b00100,0b00100,0b00100
};
*/

  /*
  for ( byte nLcd = 0; nLcd < 8; nLcd++ )
    lcd.createChar( nLcd, fader[nLcd]);
  */
  
  
        
      /*
      lcd.setCursor( 10, 1 );
      lcd.print( lights, DEC );
      */

      /*
      // Draw the fader on the screen at its g_channel position
      byte lcdfader = g_velocity >> 4;
      lcd.setCursor( (g_channel - 1) % 16, g_channel > 16 );
      lcd.write( lcdfader );
      lcd.setCursor( 15, 1 );
      lcd.write( "l" );
      break;
      */
      


