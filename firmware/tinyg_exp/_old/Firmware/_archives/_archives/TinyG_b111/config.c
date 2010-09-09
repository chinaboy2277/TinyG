/*
  config.c - eeprom and compile time configuration handling 
  Part of Grbl

  Copyright (c) 2009 Simen Svale Skogsrud

  Grbl is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Grbl is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/
/* 
  TinyG Notes:
  Modified Grbl to support Xmega family processors
  Modifications Copyright (c) 2010 Alden S. Hart, Jr.

  Enhanced the $ config with mnemonic system. See confih.h
*/

#include <avr/io.h>
#include <math.h>
#include "xmega_support.h"
#include "nuts_bolts.h"
#include "config.h"
#include "eeprom_xmega.h"
#include "wiring_serial.h"
#include <avr/pgmspace.h>

void _config_computed(void);		// prototypes for local functions (helper functions) 

void reset_settings() 
{
	settings.steps_per_mm[0] = X_STEPS_PER_MM;
	settings.steps_per_mm[1] = Y_STEPS_PER_MM;
	settings.steps_per_mm[2] = Z_STEPS_PER_MM;
	settings.default_feed_rate = DEFAULT_FEEDRATE;
	settings.default_seek_rate = DEFAULT_SEEKRATE;
	settings.mm_per_arc_segment = MM_PER_ARC_SEGMENT;
}

void dump_settings() 
{
	printPgmString(PSTR("$0 = ")); printFloat(settings.steps_per_mm[0]);
	printPgmString(PSTR(" (steps/mm x)\r\n$1 = ")); printFloat(settings.steps_per_mm[1]);
	printPgmString(PSTR(" (steps/mm y)\r\n$2 = ")); printFloat(settings.steps_per_mm[2]);
	printPgmString(PSTR(" (microseconds step pulse)\r\n$4 = ")); printFloat(settings.default_feed_rate);
	printPgmString(PSTR(" (mm/sec default feed rate)\r\n$5 = ")); printFloat(settings.default_seek_rate);
	printPgmString(PSTR(" (mm/sec default seek rate)\r\n$6 = ")); printFloat(settings.mm_per_arc_segment);
	printPgmString(PSTR(")\r\n\r\n'$x=value' to set parameter or just '$' to dump current settings\r\n"));
}

int read_settings() 
{
	uint8_t version = eeprom_get_char(0);	// Check version-byte of eeprom

	if (version != SETTINGS_VERSION) {		// Read settings-record and check checksum
		return(FALSE); 
	} 
  	if (!(memcpy_from_eeprom_with_checksum((char*)&settings, 1, sizeof(struct Settings)))) {
    	return(FALSE);
  	}
  	return(TRUE);
}

void write_settings() 
{
	eeprom_put_char(0, SETTINGS_VERSION);
	memcpy_to_eeprom_with_checksum(1, (char*)&settings, sizeof(struct Settings));
}

/* store_setting() - a helper method to set settings from command line */

void store_setting(int parameter, double value) 
{
	switch(parameter) {
    	case 0: case 1: case 2: settings.steps_per_mm[parameter] = value; break;
//    	case 3: settings.pulse_microseconds = round(value); break;
    	case 4: settings.default_feed_rate = value; break;
    	case 5: settings.default_seek_rate = value; break;
    	case 6: settings.mm_per_arc_segment = value; break;
//    	case 7: settings.invert_mask = trunc(value); break;
    	default: printPgmString(PSTR("Unknown parameter\r\n"));
    	return;
  	}
  	write_settings();
	printPgmString(PSTR("Stored new setting\r\n"));
}


/* config_init() - initialize config system */

void config_init() 
{
/*
	if(read_settings()) {
    	printPgmString(PSTR("'$' to dump current settings\r\n"));
	} else {
    	printPgmString(("EEPROM blank. Rewrote default settings:\r\n"));
    	reset_settings();
    	write_settings();
    	dump_settings();
	}
*/
	config_reset();
}


/* config_reset() - load default settings into config */

void config_reset()
{
	cfg.config_version = CONFIG_VERSION;
	cfg.mm_per_arc_segment = MM_PER_ARC_SEGMENT;

	cfg.seek_steps_sec[X_AXIS] = X_SEEK_WHOLE_STEPS_PER_SEC;
	cfg.seek_steps_sec[Y_AXIS] = Y_SEEK_WHOLE_STEPS_PER_SEC;
	cfg.seek_steps_sec[Z_AXIS] = Z_SEEK_WHOLE_STEPS_PER_SEC;
	cfg.seek_steps_sec[A_AXIS] = A_SEEK_WHOLE_STEPS_PER_SEC;
	
	cfg.feed_steps_sec[X_AXIS] = X_FEED_WHOLE_STEPS_PER_SEC;
	cfg.feed_steps_sec[Y_AXIS] = Y_FEED_WHOLE_STEPS_PER_SEC;
	cfg.feed_steps_sec[Z_AXIS] = Z_FEED_WHOLE_STEPS_PER_SEC;
	cfg.feed_steps_sec[A_AXIS] = A_FEED_WHOLE_STEPS_PER_SEC;
	
	cfg.degree_per_step[X_AXIS] = X_DEGREE_PER_WHOLE_STEP;
	cfg.degree_per_step[Y_AXIS] = Y_DEGREE_PER_WHOLE_STEP;
	cfg.degree_per_step[Z_AXIS] = Z_DEGREE_PER_WHOLE_STEP;
	cfg.degree_per_step[A_AXIS] = A_DEGREE_PER_WHOLE_STEP;
	
	cfg.mm_per_rev[X_AXIS] = X_MM_PER_REVOLUTION;
	cfg.mm_per_rev[Y_AXIS] = Y_MM_PER_REVOLUTION;
	cfg.mm_per_rev[Z_AXIS] = Z_MM_PER_REVOLUTION;
	cfg.mm_per_rev[A_AXIS] = A_MM_PER_REVOLUTION;
	
	cfg.mm_travel[X_AXIS] = X_MM_TRAVEL;
	cfg.mm_travel[Y_AXIS] = Y_MM_TRAVEL;
	cfg.mm_travel[Z_AXIS] = Z_MM_TRAVEL;
	cfg.mm_travel[A_AXIS] = A_MM_TRAVEL;
	
	cfg.microstep[X_AXIS] = X_MICROSTEPS;
	cfg.microstep[Y_AXIS] = Y_MICROSTEPS;
	cfg.microstep[Z_AXIS] = Z_MICROSTEPS;
	cfg.microstep[A_AXIS] = A_MICROSTEPS;

	cfg.limit_enable[X_AXIS] = X_LIMIT_ENABLE;
	cfg.limit_enable[Y_AXIS] = Y_LIMIT_ENABLE;
	cfg.limit_enable[Z_AXIS] = Z_LIMIT_ENABLE;
	cfg.limit_enable[A_AXIS] = A_LIMIT_ENABLE;
	
	cfg.low_pwr_idle[X_AXIS] = X_LOW_POWER_IDLE_ENABLE;
	cfg.low_pwr_idle[Y_AXIS] = Y_LOW_POWER_IDLE_ENABLE;
	cfg.low_pwr_idle[Z_AXIS] = Z_LOW_POWER_IDLE_ENABLE;
	cfg.low_pwr_idle[A_AXIS] = A_LOW_POWER_IDLE_ENABLE;
	
	_config_computed();		// generate computed values from the above
}

/* _config_computed() - helper function to generate computed config values 
	call this every time you change any configs
*/

void _config_computed() 
{
	// 360 / (degree_per_step/microstep) / mm_per_rev
	for (int i = X_AXIS; i <= A_AXIS; i++) {
		cfg.steps_per_mm[i] = (360 / (cfg.degree_per_step[i] / cfg.microstep[i])) / cfg.mm_per_rev[i];
	}

	// feed_steps_sec / (360/degree_per_step/microstep)
	cfg.default_feed_rate = (cfg.feed_steps_sec[X_AXIS] * cfg.microstep[X_AXIS]) / 
							(360/(cfg.degree_per_step[X_AXIS] / cfg.microstep[X_AXIS]));

	// seek_steps_sec / (360/degree_per_step/microstep)
	cfg.default_seek_rate = (cfg.seek_steps_sec[X_AXIS] * cfg.microstep[X_AXIS]) / 
							(360/(cfg.degree_per_step[X_AXIS] / cfg.microstep[X_AXIS]));
}

/* config_read() - read config data from program ROM into the config struct */

int config_read()
{
	uint8_t version = eeprom_get_char(0);	// Check version-byte of eeprom

	if (version != CONFIG_VERSION) {		// Read config-record and check checksum
		return(FALSE); 
	} 
  	if (!(memcpy_from_eeprom_with_checksum((char*)&cfg, 0, sizeof(struct Config)))) {
    	return(FALSE);
  	}
  	return(TRUE);
}



/* config_write() - write config struct to program ROM */

void config_write()
{
//	eeprom_put_char(0, CONFIG_VERSION);
	memcpy_to_eeprom_with_checksum(0, (char*)&cfg, sizeof(struct Config));

}

/* config_parse() - parse a config string into the config record

  Config string may consist of one or more tag=value pairs 
 
  Supported tags (axes X,Y,Z,A are supported - only X is shown)
	mm_arc_segment		0.1		arc drawing resolution in millimeters per segment 
	x_seek_steps_sec	1800	max seek whole steps per second for X axis
	x_feed_steps_sec	1200	max feed whole steps per second for X axis
	x_degree_step		1.8		degrees per whole step for X axis
	x_mm_rev			2.54	millimeters of travel per revolution of X axis
	x_mm_travel			406		millimeters of travel in X dimension (total envelope)
  	x_microstep			8		microsteps to apply for X axis steps
	x_low_pwr_idle		1		1=low power idle mode, 0=full power idle mode 
	x_limit_enable		1		1=max limit switch enabled, 0=not enabled

  Tags are case and punctuation insensitive 
  All whitespace is ignored 
  Tags are only parsed to the point of uniqueness
  The following are equivalent
	z_seek_steps_sec
	z-seek-steps-sec
	z_seek_steps_*&*&^@#*&^_sec
	zSeekStepsSec
	zs
	ZS

  Tags and values are separated by '=' sign
  Whitespace is optional and ignored
  Values are read as floating point numbers and cast to proper internal types
  Integers received as fractional numbers are truncated (this shouldn't happen)
  
*/

/* PacketType Strings - for output as ASCII
   This is an example of how to put an entire string table into program memory
   The order of strings in the table must match order of prPacketTypes enum
   Access is by: (PGM_P)pgm_read_word(&(prPacketTypeStrings[i]))
     where i is the prPacketTypes enum, e.g. packData
   ref: http://www.cs.mun.ca/~paul/cs4723/material/atmel/avr-libc-user-manual-1.6.5/pgmspace.html
 */

char prPacketTypeStringInvalid[] PROGMEM = "invalid";
char prPacketTypeStringTalk[] 	 PROGMEM = "talk";
char prPacketTypeStringData[] 	 PROGMEM = "data";
char prPacketTypeStringAck[] 	 PROGMEM = "ack";
char prPacketTypeStringBack[] 	 PROGMEM = "back";
char prPacketTypeStringQuery[]	 PROGMEM = "query";
char prPacketTypeStringNodeid[]	 PROGMEM = "nodeid";
char prPacketTypeStringConfig[]	 PROGMEM = "config";
char prPacketTypeStringPing[]	 PROGMEM = "ping";
char prPacketTypeStringStart[]	 PROGMEM = "start";
char prPacketTypeStringHush[]	 PROGMEM = "hush";
char prPacketTypeStringResend[]	 PROGMEM = "resend";
char prPacketTypeStringEnd[]	 PROGMEM = "end";

PGM_P prPacketTypeStrings[] PROGMEM = 
{
	prPacketTypeStringInvalid,
	prPacketTypeStringTalk,
	prPacketTypeStringData,
	prPacketTypeStringAck,
	prPacketTypeStringBack,
	prPacketTypeStringQuery,
	prPacketTypeStringNodeid,
	prPacketTypeStringConfig,
	prPacketTypeStringPing,
	prPacketTypeStringStart,
	prPacketTypeStringHush,
	prPacketTypeStringResend,
	prPacketTypeStringEnd
};

	char configline[40];	// normalized configuration line
	int j = 0;					// RAM buffer index (textline)
	char c;

void config_parse(char *textline)
{
//	char configline[40];	// normalized configuration line
	int i = 0;					// ROM buffer index (int allows for > 256 chars)
//	int j = 0;					// RAM buffer index (textline)
//	char c;

	while ((c = textline[i++]) != 0) {					// normalize config line into internal buffer
		if ((c == '=') || (c == '-') || (c == '-')) {	// save special characters
			configline[j++] = c;		
		} else if (c >= 'a' && c <= 'z') {				// convert lower to upper and save
			configline[j++] = c-'a'+'A';
		} else if (c >= 'A' && c <= 'Z') {				// save upper case letters
			configline[j++] = c;
		} else if (c >= '0' && c <= '9') {				// save numbers
			configline[j++] = c;
		}
	}
//	config_write();
//	printPgmString(PSTR("Stored new setting\r\n"));
}

/* config_test() - generate some strings for the parser and test EEPROM read and write */

char configs_P[] PROGMEM = "\
x_seek_steps_sec = 2000 \r\
y_seek_steps_sec = 2000 \r\
z_seek_steps_sec = 2000 \r\
a_seek_steps_sec = 2000";

//	char textline[40];
//	char c;
//	int j = 0;					// RAM buffer index (textline)

void config_test()
{
	char textline[40];
	int i = 0;					// ROM buffer index (int allows for > 256 chars)
	int j = 0;					// RAM buffer index (textline)
	char c;

	// feed the parser one line at a time
	while ((c = pgm_read_byte(&configs_P[i++])) != 0) {
		if ((c == '\r') || (c == '\n') || (c == ';')) {	// line complete
			textline[j] = 0;							// terminate the string
			config_parse(textline);						// parse line 
			j = 0;			
		} else if (c <= ' ') { 							// toss whitespace & ctrls
		} else {
			textline[j++] = c;							// put characters into line
		}
	}
}
