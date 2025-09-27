#ifndef _PAPITO_H
#define _PAPITO_H

#include <stdlib.h>
#include <stdio.h>
#include <papi.h>

// Internal functions
void input();
void handle_error (int retval);

// User functions
void papito_init();
void papito_start();
void papito_end();

#endif
