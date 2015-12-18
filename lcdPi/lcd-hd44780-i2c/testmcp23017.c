/*
//  ===========================================================================

    testmcp23017:

    Tests MCP23017 driver for the Raspberry Pi.

    Copyright 2015 Darren Faulke <darren@alidaf.co.uk>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.

//  ===========================================================================
*/

#define Version "Version 0.1"

/*
//  ---------------------------------------------------------------------------

    Compile with:

    gcc testmcp23017.c mcp23017.c -Wall -o testmcp23017

    Also use the following flags for Raspberry Pi optimisation:
        -march=armv6 -mtune=arm1176jzf-s -mfloat-abi=hard -mfpu=vfp
        -ffast-math -pipe -O3

//  ---------------------------------------------------------------------------

    Authors:        D.Faulke    17/12/2015  This program.

    Contributors:

    Changelog:

        v0.1    Original version.

//  Information. --------------------------------------------------------------

    The MCP23017 is an I2C bus operated 16-bit I/O port expander.

    For testing, LEDs were connected via a breadboard as follows:

                        +-----------( )-----------+
                        |  Fn  | pin | pin |  Fn  |
             100R  LED  |------+-----+-----+------|
        .---/\/\/--|<|--| GPB0 |  01 | 28  | GPA7 |---/ ---.
        |---/\/\/--|<|--| GPB1 |  02 | 27  | GPA6 |---/ ---|
        |---/\/\/--|<|--| GPB2 |  03 | 26  | GPA5 |---/ ---|
        |---/\/\/--|<|--| GPB3 |  04 | 25  | GPA4 |---/ ---|  8 port
        |---/\/\/--|<|--| GPB4 |  05 | 24  | GPA3 |---/ ---| dip switch
        |---/\/\/--|<|--| GPB5 |  06 | 23  | GPA2 |---/ ---|
        |---/\/\/--|<|--| GPB6 |  07 | 22  | GPA1 |---/ ---|
        |---/\/\/--|<|--| GPB7 |  08 | 21  | GPA0 |---/ ---|
        |     +3.3V <---|  VDD |  09 | 20  | INTA |        |
 GND <--'---------------|  VSS |  10 | 19  | INTB |        |
                        |   NC |  11 | 18  | RST  |--------'----> +3.3V.
            I2C CLK <---|  SCL |  12 | 17  | A2   |---> GND }
            I2C I/O <---|  SDA |  13 | 16  | A1   |---> GND } Address = 0x20.
                        |   NC |  14 | 15  | A0   |---> GND }
                        +-------------------------+

    The LEDs have a forward voltage and current of 1.8V and 20mA respectively
    so 100Ohm resistors are reasonable although 80Ohms is more ideal.

                    R = (3.3 - 1.8) / 20x10-3 = 75Ohm.

//  ---------------------------------------------------------------------------
*/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>

#include "mcp23017.h"

int main()
{

    int8_t err = 0; // Return code.
    int8_t num = 1; // Number of MCP23017 devices.

    // Initialise MCP23017. Only using 1 but should work for up to 8.
    err = mcp23017Init( 0x20 );
    if ( err < 0 )
    {
        printf( "Couldn't init.\n" );
        return -1;
    }

    // Print properties for each device.
    printf( "Properties.\n" );

    uint8_t i; // Loop counter.

    for ( i = 0; i < num; i++ )
    {
        // Start off with BANK = 0.
        mcp23017[i]->bank = 0;
        mcp23017WriteRegisterByte( mcp23017[i], IOCONA, 0x00 );

        // Make sure MCP23017s have been initialised OK.
        printf( "\tDevice %d:\n", i );
        printf( "\tHandle = %d,\n", mcp23017[i]->id );
        printf( "\tAddress = 0x%02x,\n", mcp23017[i]->addr );
        printf( "\tBank mode = %1d.\n", mcp23017[i]->bank );

        // Set direction of GPIOs and clear latches.
        mcp23017WriteRegisterByte( mcp23017[i], IODIRA, 0xff ); // Input.
        mcp23017WriteRegisterByte( mcp23017[i], IODIRB, 0x00 ); // Output.

        // Writes to latches are the same as writes to GPIOs.
        mcp23017WriteRegisterByte( mcp23017[i], OLATA, 0x00 ); // Clear pins.
        mcp23017WriteRegisterByte( mcp23017[i], OLATB, 0x00 ); // Clear pins.

    }
    printf( "\n" );

    // Test setting BANK modes:

    uint8_t j, k; // Loop counters.
    uint8_t bank; // Temp storage for bank bit.

    for ( i = 0; i < num; i++ ) // For each MCP23017.
    {
        printf( "Using MCP23017 %d.\n", i );

        for ( j = 0; j < 2; j++ ) // Toggle BANK bit twice.
        {
            printf( "\tTrying BANK = %d.\n", mcp23017[i]->bank );

            for ( k = 0; k < 0xff; k++ ) // Write 0x00 to 0xff.
            {
                mcp23017WriteRegisterWord( mcp23017[i], OLATB, k );
                usleep( 100000 );
            }

            // Reset all LEDs.
            mcp23017WriteRegisterByte( mcp23017[i], OLATB, 0x00 );

            // Toggle BANK bit.
            mcp23017[i]->bank = !mcp23017[i]->bank;
            bank = mcp23017[i]->bank * 0x80;
            mcp23017WriteRegisterByte( mcp23017[i], IOCONA, bank );
        }

        // Next MCP23017.
        printf( "\n" );
    }

    // Now test input.
    uint8_t data, last = 0x00;
    printf( "Now reading inputs on PORT A and writing to PORT B.\n" );

    while ( !getchar() ) // Read inputs until keypress.
    {
        // Read switches and write byte value to LEDs.
        data = mcp23017ReadRegisterByte( mcp23017[0], GPIOA );
        if ( data != last )
        {
            printf( "Input changed to 0x%02x.\n", data );
            last = data;
        }
        mcp23017WriteRegisterByte( mcp23017[0], OLATB, data );
        sleep( 1 );
    }
}
