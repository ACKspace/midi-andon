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

byte g_channel[ 2 ];
byte g_velocity[ 2 ];
byte g_midimode[ 2 ];
byte g_nSelectedLamp;
byte g_nProgramtick;
byte g_nMidiVelocities[127] = {0};
// TODO: 254 g_velocities: channels 1-112 are used
//  1-32: channel 1-32 fader volume
// 33-48: bus 1-16
// 49-52: aux send master
// 53-56: fx send master
// 57-60: fx return 1-4 (L of pair)
// 61: main mix
// 62-63: NOT USED
// 64-95: pan channel 1-32
// 96-99: fx pan return 1-4 (L of pair)
// 100: master pan
// 101-103: NOT USED
// 104: Channel mute (0,1-61) on
// 105: Channel mute (0,1-61) off
// 106: snapshot save (0,1-61) on
// 107: snapshot save (0,1-61) off
// 108: automation rec/play set (0,1-61) manual
// 109: automation rec/play set (0,1-61) rec ready
// 110: automation rec/play set (0,1-61) record
// 111: automation rec/play set (0,1-61) fadeback
// 112: automation rec/play set (0,1-61) play

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
#define LAMP1 6
#define LAMP2 5
#define LAMP3 4
#define LAMP4 3
//Note that LAMP5 is not connected but is wired next to lamp 1 (white cable)
#define RELAY 2

#define MIDI_START          0x80
#define MIDI_PROGRAM_CHANGE 0xB0
#define MIDI_MASK_OMNI      0xf0
#define MIDI_INVALID        0xFF
#define MIDI_FLAG_ABOVE     0x80

////////////////////////////////////////////////////////////////////
// setup
////////////////////////////////////////////////////////////////////
void setup()  
{
  // wait for serial port to connect. Needed for Leonardo only
  // Open serial communications and wait for port to open:
  Serial.begin( 115200 );
  while (!Serial);
  Serial.println( "initializing.." );
  Serial.println( "outputs" );
  pinMode( ACTIVITYLED, OUTPUT );
  pinMode( PUSHBUTTON, INPUT_PULLUP );
  for ( byte nPin = RELAY; nPin <= LAMP1; nPin++ )
    pinMode( nPin, OUTPUT );

  Serial.println( "midi" );
  while (!Serial1);
  Serial1.begin( 31250 );
  while (!Serial2);
  Serial2.begin( 31250 );

  delay( 200 );
  // Display version/revision
  digitalWrite( LAMP2, HIGH );
  Serial.println( "software v2" );
  delay( 2000 );

  resetLamp();
  // TODO: verify ROM
  Serial.print( "verifying ROM." );
  delay( 500 );
  digitalWrite( LAMP1, HIGH );
  Serial.print( "." );
  delay( 500 );
  digitalWrite( LAMP2, HIGH );
  Serial.print( "." );
  delay( 500 );
  digitalWrite( LAMP3, HIGH );
  Serial.print( "." );
  delay( 500 );
  digitalWrite( LAMP4, HIGH );
  Serial.print( "." );
  delay( 500 );
  Serial.println( "ok" );
  resetLamp();  

  // Assume program change
  g_midimode[ 1 ] = MIDI_PROGRAM_CHANGE;
  g_midimode[ 2 ] = MIDI_PROGRAM_CHANGE;

  g_nHoldCounter = 0;
  g_buttonState = ButtonState::None;
  g_programMode = ProgramMode::Normal; //Demo or Normal

  Serial.println( "done.." );

  g_nProgramtick = 0;
  g_nSelectedLamp = 0;

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

  //handleMidiMessage( Serial1, 0 );
  handleMidiMessage( Serial2, 1 );
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
  for ( byte nPin = RELAY; nPin <= LAMP1; nPin++ )
    digitalWrite( nPin, LOW );
}

////////////////////////////////////////////////////////////////////
// handleMidiMessage
////////////////////////////////////////////////////////////////////
void handleMidiMessage( const Stream& _serial, uint8_t _nIndex )
{
  byte initialbyte;
  
  // Always clear g_channel to make sure we only handle it once per tick
  g_channel[ _nIndex ] = MIDI_INVALID;

  if( _serial.available() > 0 )
  {
    digitalWrite( ACTIVITYLED, HIGH );
    initialbyte = _serial.read(); 
#ifdef DEBUG
  Serial.write( "midi message: " );
  Serial.println( initialbyte, HEX ); 
#endif
    
    // Start of MIDI message? Assign mode
    if ( (initialbyte & MIDI_START) == MIDI_START )
    {
      g_midimode[ _nIndex ] = initialbyte;
      // Clear g_channel byte
      //g_channel = MIDI_INVALID;
    }
    else
    {
      // No mode change, Copy over byte as channel
      g_channel[ _nIndex ] = initialbyte + (1-_nIndex) * 128;
    }
    
#ifdef DEBUG
    Serial.print( "midi cmd:" );
    Serial.println( g_midimode[ _nIndex ], HEX );
#endif
    // Mask out g_channel (omni-receive) and check which midi command we have
    switch ( g_midimode[ _nIndex ] & MIDI_MASK_OMNI )
    {
      case MIDI_PROGRAM_CHANGE: // Program change (3 bytes)
#ifdef DEBUG
  Serial.write( "program change: " );
#endif

        // No g_channel set yet?
        if ( g_channel[ _nIndex ] == MIDI_INVALID )
        {
          while( !_serial.available() );
          g_channel[ _nIndex ] = _serial.read() + (1-_nIndex) * 128;
#ifdef DEBUG
  Serial.print( g_channel[ _nIndex ], HEX );
  Serial.print( " " );
#endif
        }

        while( !_serial.available() );
        g_velocity[ _nIndex ] = _serial.read();
#ifdef DEBUG
  Serial.print( g_velocity[ _nIndex ], HEX );
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
  g_nProgramtick++;

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
      if ( g_nProgramtick > 50 && g_nProgramtick < 100 )
        digitalWrite( LAMP1, HIGH );
      else
        digitalWrite( LAMP1, LOW );

      if ( g_nProgramtick > 100 && g_nProgramtick < 150 )
        digitalWrite( LAMP2, HIGH );
      else
        digitalWrite( LAMP2, LOW );

      if ( g_nProgramtick > 150 && g_nProgramtick < 200 )
        digitalWrite( LAMP3, HIGH );
      else
        digitalWrite( LAMP3, LOW );

      if ( g_nProgramtick > 200 && g_nProgramtick < 250 )
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
      for ( uint8_t nIndex = 0; nIndex < 2; nIndex++ )
      {
        if ( g_channel[ nIndex ] == MIDI_INVALID )
          continue;
  
        // Updated value?
        if ( g_nMidiVelocities[ g_channel[ nIndex ] ] == g_velocity[ nIndex ] )
          continue;

        g_nMidiVelocities[ g_channel[ nIndex ] ] = g_velocity[ nIndex ];
        
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
  
        // Relay, tied to light 4 (red)
        digitalWrite( RELAY, lights & ( 1 << 3 ) );
      }
  }
};

////////////////////////////////////////////////////////////////////
// doConfigTick
////////////////////////////////////////////////////////////////////
void doConfigTick()
{
  g_nProgramtick++;

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

      g_nSelectedLamp++;
      if ( g_nSelectedLamp >= 5 )
        g_nSelectedLamp = 0;
      break;

    default:
      if ( g_nProgramtick > 127 )
        digitalWrite( g_nSelectedLamp + 3, HIGH );
      else
        digitalWrite( g_nSelectedLamp + 3, LOW );

      break;
  }
};


#define MEM_SIZE 64
byte determineLights( )
{
    byte lights = 0;            // Light bitmask
    byte currentLight = 0;      // Current active light
    byte nextLightAddress = 0;  // Next light's memory address

    byte memVal;
    byte chan = MIDI_INVALID;

    // Walk the memory
    for ( byte idx = 0; idx < MEM_SIZE; idx++ )
    {
        memVal = readMemory( idx );

        if ( !idx )
        {
            // First byte is always the next light pos
            nextLightAddress = memVal;
        }
        else if ( idx == nextLightAddress )
        {
            // Break out of loop if all lights are read or empty memory
            if ( (currentLight >= 3) || !memVal || memVal == 0xFF )
                break;

            // We reached the next light pos, increase the light counter
            currentLight++;

            // Get the address of the next light
            nextLightAddress = memVal;

            // Reset channel
            chan = MIDI_INVALID;
        }
        else if ( chan == MIDI_INVALID )
        {
            chan = memVal;
        }
        else
        {
            // Check the current stored value with the corresponding midi velocity and set bit if either:
            // a. above flag set and velocity > memory value (without flag)
            // b. no flag and velocity < memory value
            if ( ( (memVal & MIDI_FLAG_ABOVE) && g_nMidiVelocities[ chan ] > ( memVal & ( MIDI_FLAG_ABOVE - 1 ) ) )
              || ( ( !(memVal & MIDI_FLAG_ABOVE) && g_nMidiVelocities[ chan ] < memVal ) )
             )
                lights |= (1 << currentLight );

            // Reset channel, so the next memory value will be assigned as channel
            chan = MIDI_INVALID;
        }
    }

    return lights;
};

////////////////////////////////////////////////////////////////////
// 
////////////////////////////////////////////////////////////////////
byte readMemory( short _nAddr )
{
  // TODO: implement o-buffer ROM
  //byte fakeMem[14] = { 7, 1, 10 | MIDI_FLAG_ABOVE, 2, 15 | MIDI_FLAG_ABOVE, 3, 100, 0, 4, 100 | MIDI_FLAG_ABOVE, 5, 10 | MIDI_FLAG_ABOVE, 6, 10 };

  // <absolute index of next color>, [<g_channel>, <threshold>, ...]
  // (| MIDI_FLAG_ABOVE means above, else below)
  // blue, green, orange, red + relay
byte fakeMem[ ] = {
  9, 29, 10 | MIDI_FLAG_ABOVE, // blue
     30, 10 | MIDI_FLAG_ABOVE,
     31, 10 | MIDI_FLAG_ABOVE,
     32, 10 | MIDI_FLAG_ABOVE,
 12, 61, 10,                   // green
 15, 61, 10 | MIDI_FLAG_ABOVE, // orange
 64,  1, 10 | MIDI_FLAG_ABOVE, // red
      2, 10 | MIDI_FLAG_ABOVE,
      3, 10 | MIDI_FLAG_ABOVE,
      4, 10 | MIDI_FLAG_ABOVE,
      5, 10 | MIDI_FLAG_ABOVE,
      6, 10 | MIDI_FLAG_ABOVE,
      7, 10 | MIDI_FLAG_ABOVE,
      8, 10 | MIDI_FLAG_ABOVE,
      9, 10 | MIDI_FLAG_ABOVE,
     10, 10 | MIDI_FLAG_ABOVE,
     11, 10 | MIDI_FLAG_ABOVE,
     12, 10 | MIDI_FLAG_ABOVE,
     // jack/cinch in
     17, 10 | MIDI_FLAG_ABOVE,
     18, 10 | MIDI_FLAG_ABOVE,
     19, 10 | MIDI_FLAG_ABOVE,
     20, 10 | MIDI_FLAG_ABOVE,
     21, 10 | MIDI_FLAG_ABOVE,
     22, 10 | MIDI_FLAG_ABOVE,
     23, 10 | MIDI_FLAG_ABOVE,
     24, 10 | MIDI_FLAG_ABOVE,
     25, 10 | MIDI_FLAG_ABOVE,
     26, 10 | MIDI_FLAG_ABOVE,
     27, 10 | MIDI_FLAG_ABOVE,
     28, 10 | MIDI_FLAG_ABOVE,
};
  /*
  byte fakeMem[ ] = {  3, 13, 10 | MIDI_FLAG_ABOVE, // blue
                       6, 61, 10,                   // green
                      39,  1, 10 | MIDI_FLAG_ABOVE, // orange
                           2, 10 | MIDI_FLAG_ABOVE,
                           3, 10 | MIDI_FLAG_ABOVE,
                           4, 10 | MIDI_FLAG_ABOVE,
                           5, 10 | MIDI_FLAG_ABOVE,
                           6, 10 | MIDI_FLAG_ABOVE,
                           7, 10 | MIDI_FLAG_ABOVE,
                           8, 10 | MIDI_FLAG_ABOVE,
                           9, 10 | MIDI_FLAG_ABOVE,
                          10, 10 | MIDI_FLAG_ABOVE,
                          11, 10 | MIDI_FLAG_ABOVE,
                          12, 10 | MIDI_FLAG_ABOVE,
                          13, 10 | MIDI_FLAG_ABOVE,
                          14, 10 | MIDI_FLAG_ABOVE,
                          15, 10 | MIDI_FLAG_ABOVE,
                          16, 10 | MIDI_FLAG_ABOVE,
                       0, 61,  9 | MIDI_FLAG_ABOVE, // red
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
      


