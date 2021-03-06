//  ===========================================================================
/*
    testmeterPi-ncurses:

    Tests meterPi using ncurses output on terminal.

    Copyright 2016 Darren Faulke <darren@alidaf.co.uk>

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
//  ===========================================================================
/*
    Compile with:

        gcc -c -Wall meterPi.c testmeterPi-ncurses.c -o testmeterPi-ncurses
               -lm -lpthread -lrt -lncurses

    For Raspberry Pi v1 optimisation use the following flags:

        -march=armv6zk -mtune=arm1176jzf-s -mfloat-abi=hard -mfpu=vfp
        -ffast-math -pipe -O3

    For Raspberry Pi v2 optimisation use the following flags:

        -march=armv7-a -mtune=cortex-a7 -mfloat-abi=hard -mfpu=neon-vfpv4
        -ffast-math -pipe -O3
*/
//  ===========================================================================
/*
    Authors:        D.Faulke            13/02/2016

    Contributors:
*/
//  ===========================================================================

//  Installed libraries -------------------------------------------------------

#include <stdbool.h>

#include <stdio.h>
#include <stdint.h>
#include <sys/time.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

#include <ncurses.h>

//  Local libraries -------------------------------------------------------

#include "meterPi.h"


//  Functions. ----------------------------------------------------------------

#define METER_LEVELS 41
#define METER_DELAY  5000  // Need a proper timer to match sample rate.
/*
    44100 Hz = 22.7 us.
    48000 Hz = 20.8 us.

    but we are sampling two channels so sample rate is halved, i.e.
    44100 Hz = 45.4 us.
    48000 Hz = 41.7 us.
*/
#define CALIBRATION_LOOPS 300

//  ---------------------------------------------------------------------------
//  Produces string representations of the peak meters.
//  ---------------------------------------------------------------------------
/*
    This is intended for a small LCD (16x2 or similar) or terminal output.
*/
void get_peak_strings( struct peak_meter_t peak_meter,
                       char dB_string[METER_CHANNELS][METER_LEVELS + 1] )
{
    uint8_t channel;
    uint8_t i;

    for ( channel = 0; channel < METER_CHANNELS; channel++ )
    {
        for ( i = 0; i < peak_meter.num_levels; i++ )
        {
            if (( i <= peak_meter.bar_index[channel] ) ||
                ( i == peak_meter.dot_index[channel] ))
                dB_string[channel][i] = '=';
            else
                dB_string[channel][i] = ' ';
        }
        dB_string[channel][i] = '\0';
    }
}


//  ---------------------------------------------------------------------------
//  Reverses a string passed as *buffer between start and end.
//  ---------------------------------------------------------------------------
static void reverse_string( char *buffer, size_t start, size_t end )
{
    char temp;
    while ( start < end )
    {
        end--;
        temp = buffer[start];
        buffer[start] = buffer[end];
        buffer[end] = temp;
        start++;
    }
    return;
};


//  ---------------------------------------------------------------------------
//  Retruns elapsed time in microseconds.
//  ---------------------------------------------------------------------------
uint32_t time_elapsed( struct timeval start, struct timeval end )
{
    uint32_t start_us, end_us, diff_us = 0;

    start_us = (uint32_t) start.tv_sec * 1000000 + (uint32_t) start.tv_usec;
    end_us   = (uint32_t) end.tv_sec   * 1000000 + (uint32_t) end.tv_usec;

    diff_us = end_us - start_us;

    return diff_us;
}

//  ---------------------------------------------------------------------------
//  Main (functional test).
//  ---------------------------------------------------------------------------
int main( void )
{
    struct timeval start, end;
    uint32_t diff;
    uint32_t elapsed; // Elapsed time in milliseconds.
    uint16_t i;

    struct peak_meter_t peak_meter =
    {
        .int_time   = 5,
        .samples    = 2,
        .hold_time  = 1000,
        .hold_incs  = 50,
        .fall_time  = 50,
        .fall_incs  = 5,
        .over_peaks = 10,
        .over_time  = 3000,
        .over_incs  = 150,
        .num_levels = 41,
        .floor      = -96,
        .reference  = 32768,
        .overload   = { false, false },
        .dBfs       = { 0, 0 },
        .bar_index  = { 0, 0 },
        .dot_index  = { 0, 0 },
        .elapsed    = { 0, 0 },
        .scale      =
            { -40, -39, -38, -37, -36, -35, -34, -33, -32, -31
              -30, -29, -28, -27, -26, -25, -24, -23, -22, -21,
              -20, -19, -18, -17, -16, -15, -14, -13, -12, -11,
              -10,  -9,  -8,  -7,  -6,  -5,  -4,  -3,  -2,  -1,   0  }
    };

    // String representations for LCD display.
    char window_peak_meter[METER_CHANNELS][METER_LEVELS + 1];

    vis_check();

    // Calculate number of samples for integration time.
	peak_meter.samples = vis_get_rate() * peak_meter.int_time / 1000;
    if ( peak_meter.samples < 1 ) peak_meter.samples = 1;
    if ( peak_meter.samples > VIS_BUF_SIZE / METER_CHANNELS )
         peak_meter.samples = VIS_BUF_SIZE / METER_CHANNELS;
//    peak_meter.samples = 2; // Minimum samples for fastest response but may miss peaks.
    printf( "Samples for %dms = %d.\n", peak_meter.int_time, peak_meter.samples );

    // ncurses stuff.
    WINDOW *meter_win;
    initscr();
    cbreak();
    noecho();
    meter_win = newwin( 7, 52, 10, 10 );
    box( meter_win, 0, 0 );
    wrefresh( meter_win );
    nodelay( meter_win, TRUE );
    scrollok( meter_win, TRUE );
    curs_set( 0 );

    // Meter scale.
    mvwprintw( meter_win, 1, 2, "L" );
    mvwprintw( meter_win, 2, 2, " |....|....|....|....|....|....|....|....|" );
    mvwprintw( meter_win, 3, 2, " Calibrating. " );
    mvwprintw( meter_win, 4, 2, " |''''|''''|''''|''''|''''|''''|''''|''''|" );
    mvwprintw( meter_win, 5, 2, "R" );

    // Create foreground/background colour pairs for meters.
    start_color();
    init_pair( 1, COLOR_GREEN, COLOR_BLACK );
    init_pair( 2, COLOR_YELLOW, COLOR_BLACK );
    init_pair( 3, COLOR_RED, COLOR_BLACK );

    // Calibration.
    gettimeofday( &start, NULL );

    for ( i = 0; i < CALIBRATION_LOOPS; i++ )
    {
        // Get integrated peak dBFS values and indices for meter.
        get_dBfs( &peak_meter );
        get_dB_indices( &peak_meter );
        get_peak_strings( peak_meter, window_peak_meter );

        mvwprintw( meter_win, 3, 2, " Calibrating. Loop %d ", i + 1 );

        // Draw meters.
        mvwprintw( meter_win, 1, 3, "%s", window_peak_meter[0] );
        mvwprintw( meter_win, 5, 3, "%s", window_peak_meter[1] );

        // Overload indicators.
        if ( peak_meter.overload[0] == true )
            mvwprintw( meter_win, 1, 45, "OVER" );
        else
            mvwprintw( meter_win, 1, 45, "    " );
        if ( peak_meter.overload[1] == true )
            mvwprintw( meter_win, 5, 45, "OVER" );
        else
            mvwprintw( meter_win, 5, 45, "    " );

        // Meter colours.
        mvwchgat( meter_win, 1,  3, 31, A_NORMAL, 1, NULL );
        mvwchgat( meter_win, 1, 34,  5, A_NORMAL, 2, NULL );
        mvwchgat( meter_win, 1, 39, 10, A_NORMAL, 3, NULL );
        mvwchgat( meter_win, 5,  3, 31, A_NORMAL, 1, NULL );
        mvwchgat( meter_win, 5, 34,  5, A_NORMAL, 2, NULL );
        mvwchgat( meter_win, 5, 39, 10, A_NORMAL, 3, NULL );

        // Refresh ncurses window to display meters.
        wrefresh( meter_win );

        usleep( METER_DELAY );
    }

    gettimeofday( &end, NULL );
    mvwprintw( meter_win, 3, 2, " Finished calibrating." );
    wrefresh( meter_win );

    sleep( 2 );

    mvwprintw( meter_win, 3, 2, " Calculating counters." );
    wrefresh( meter_win );

    diff = time_elapsed( start, end ) / CALIBRATION_LOOPS;
    elapsed = diff / 1000;
//    elapsed = (( end.tv_sec  - start.tv_sec  ) * 1000 +
//               ( end.tv_usec - start.tv_usec ) / 1000 ) / CALIBRATION_LOOPS;

    sleep( 2 );

    mvwprintw( meter_win, 3, 2, " Loop time = %ld us.       ",
               diff - ( (uint32_t) peak_meter.int_time * 1000 ));
    wrefresh( meter_win );

    sleep( 2 );

    // Calculate counters.
    if ( elapsed < peak_meter.hold_time )
        peak_meter.hold_incs = peak_meter.hold_time / elapsed;

    if ( elapsed < peak_meter.fall_time )
        peak_meter.fall_incs = peak_meter.fall_time / elapsed;

    if ( elapsed < peak_meter.over_time )
        peak_meter.over_incs = peak_meter.over_time / elapsed;

    mvwprintw( meter_win, 3, 2, "-40  -35  -30  -25  -20  -15  -10  -5    0 dBFS" );

    int ch = ERR;
    while ( ch == ERR )
    {

        get_dBfs( &peak_meter );
        get_dB_indices( &peak_meter );
        get_peak_strings( peak_meter, window_peak_meter );

        // Print ncurses data.
        mvwprintw( meter_win, 1, 3, "%s", window_peak_meter[0] );
        mvwprintw( meter_win, 5, 3, "%s", window_peak_meter[1] );

        if ( peak_meter.overload[0] == true )
            mvwprintw( meter_win, 1, 45, "OVER" );
        else
            mvwprintw( meter_win, 1, 45, "    " );
        if ( peak_meter.overload[1] == true )
            mvwprintw( meter_win, 5, 45, "OVER" );
        else
            mvwprintw( meter_win, 5, 45, "    " );

        mvwchgat( meter_win, 1,  3, 31, A_NORMAL, 1, NULL );
        mvwchgat( meter_win, 1, 34,  5, A_NORMAL, 2, NULL );
        mvwchgat( meter_win, 1, 39, 10, A_NORMAL, 3, NULL );
        mvwchgat( meter_win, 5,  3, 31, A_NORMAL, 1, NULL );
        mvwchgat( meter_win, 5, 34,  5, A_NORMAL, 2, NULL );
        mvwchgat( meter_win, 5, 39, 10, A_NORMAL, 3, NULL );

        // Refresh ncurses window to display.
        wrefresh( meter_win );
        ch = wgetch( meter_win );
        usleep( METER_DELAY );
    }

    // Close ncurses.
    delwin( meter_win );
    endwin();

    return 0;
}
