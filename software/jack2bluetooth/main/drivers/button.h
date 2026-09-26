#pragma once

typedef enum
{
    J2B,
    B2J
} direction_state;

/********************************
 * EXTERNAL FUNCTION DECLARATIONS
 *******************************/

void on_enter_pressed(void);
void on_back_pressed(void);
void on_next_pressed(void);
void on_direction_changed(direction_state new_state);
