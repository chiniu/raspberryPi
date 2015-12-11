// ****************************************************************************
// ****************************************************************************
/*
    rotencPi:

    Rotary encoder driver for the Raspberry Pi.

    Copyright 2015 Darren Faulke <darren@alidaf.co.uk>
        Rotary encoder state machine based on algorithm by Ben Buxton.
            - see http://www.buxtronix.net.

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
*/
// ****************************************************************************
// ****************************************************************************

#define rotencPiVersion "Version 0.1"

//  Compilation:
//
//  Compile with gcc -c -fpic rotencPi.c -lwiringPi
//  Also use the following flags for Raspberry Pi optimisation:
//         -march=armv6 -mtune=arm1176jzf-s -mfloat-abi=hard -mfpu=vfp
//         -ffast-math -pipe -O3

//  Authors:        D.Faulke    10/12/2015
//  Contributors:
//
//  Changelog:
//
//  v0.1 Original version.
//

//  To Do:
//      Write GPIO and interrupt routines to replace wiringPi.
//

#include <stdio.h>
#include <string.h>
#include <argp.h>
#include <wiringPi.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdlib.h>
#include "rotencPi.h"

// ============================================================================
// Data structures
// ============================================================================

//struct encoderStruct encoder;

// ============================================================================
//  Encoder functions.
// ============================================================================

/*
    Quadrature encoding for rotary encoder:

          :   :   :   :   :   :   :   :   :
          :   +-------+   :   +-------+   :         +---+-------+-------+
          :   |   :   |   :   |   :   |   :         | P |  +ve  |  -ve  |
      A   :   |   :   |   :   |   :   |   :         | h +---+---+---+---+
      --------+   :   +-------+   :   +-------      | a | A | B | A | B |
          :   :   :   :   :   :   :   :   :         +---+---+---+---+---+
          :   :   :   :   :   :   :   :   :         | 1 | 0 | 0 | 1 | 0 |
          +-------+   :   +-------+   :   +---      | 2 | 0 | 1 | 1 | 1 |
          |   :   |   :   |   :   |   :   |         | 3 | 1 | 1 | 0 | 1 |
      B   |   :   |   :   |   :   |   :   |         | 4 | 1 | 0 | 0 | 0 |
      ----+   :   +-------+   :   +-------+         +---+---+---+---+---+
          :   :   :   :   :   :   :   :   :
        1 : 2 : 3 : 4 : 1 : 2 : 3 : 4 : 1 : 2   <- Phase
          :   :   :   :   :   :   :   :   :

    State table for full step mode:
    +---------+---------+---------+---------+
    | AB = 00 | AB = 01 | AB = 10 | AB = 11 |
    +---------+---------+---------+---------+
    | START   | C/W 1   | A/C 1   | START   |
    | C/W +   | START   | C/W X   | C/W DIR |
    | C/W +   | C/W 1   | START   | START   |
    | C/W +   | C/W 1   | C/W X   | START   |
    | A/C +   | START   | A/C 1   | START   |
    | A/C +   | A/C X   | START   | A/C DIR |
    | A/C +   | A/C X   | A/C 1   | START   |
    +---------+---------+---------+---------+
*/

const unsigned char encoderStateTable[7][4] = {{ 0x0, 0x2, 0x4, 0x0 },
                                               { 0x3, 0x0, 0x1, 0x10 },
                                               { 0x3, 0x2, 0x0, 0x0 },
                                               { 0x3, 0x2, 0x1, 0x0 },
                                               { 0x6, 0x0, 0x4, 0x0 },
                                               { 0x6, 0x5, 0x0, 0x20 },
                                               { 0x6, 0x5, 0x4, 0x0 }};

// ----------------------------------------------------------------------------
//  Returns encoder direction - should be called by an interrupt function.
// ----------------------------------------------------------------------------
/*
    Returns: +1 = +ve direction.
              0 = no change determined.
             -1 = -ve direction.
*/
void encoderDirection()
{
    // Lock thread
//    pthread_mutex_lock( &encoderBusy );

    unsigned char code = ( digitalRead( encoder.gpioB ) << 1 ) |
                           digitalRead( encoder.gpioA );
    encoder.state = encoderStateTable[ encoder.state & 0xf ][ code ];
    unsigned char direction = encoder.state & 0x30;
    if ( direction )
        encoder.direction = ( direction == 0x10 ? -1 : 1 );
    else
        encoder.direction = 0;

    // Unlock thread
//    pthread_mutex_unlock( &encoderBusy );

    return;
};


// ----------------------------------------------------------------------------
//  Initialises encoder gpio pins.
// ----------------------------------------------------------------------------
void encoderInit( unsigned char gpioA, unsigned char gpioB )
{
    wiringPiSetupGpio();

    encoder.gpioA = gpioA;
    encoder.gpioB = gpioB;

    // Set encoder pin modes.
    pinMode( encoder.gpioA, INPUT );
    pinMode( encoder.gpioB, INPUT );
    pullUpDnControl( encoder.gpioA, PUD_UP );
    pullUpDnControl( encoder.gpioB, PUD_UP );

    //  Register interrupt functions.
    wiringPiISR( encoder.gpioA, INT_EDGE_BOTH, &encoderDirection );
    wiringPiISR( encoder.gpioB, INT_EDGE_BOTH, &encoderDirection );

    return;
}

