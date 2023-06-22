// ****************************************************************************
//  sysmenu.cc                                                   DB48X project
// ****************************************************************************
//
//   File Description:
//
//     Handles the DMCP application menus on the DM42
//
//
//
//
//
//
//
//
// ****************************************************************************
//   (C) 2022 Christophe de Dinechin <christophe@dinechin.org>
//   This software is licensed under the terms outlined in LICENSE.txt
// ****************************************************************************
//   This file is part of DB48X.
//
//   DB48X is free software: you can redistribute it and/or modify
//   it under the terms outlined in the LICENSE.txt file
//
//   DB48X is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// ****************************************************************************

#include "sysmenu.h"

#include "main.h"
#include "types.h"
#include "file.h"
#include "renderer.h"

#include <cstdio>
#include <dmcp.h>


// ============================================================================
//
//    Main application menu
//
// ============================================================================

const uint8_t application_menu_items[] =
// ----------------------------------------------------------------------------
//    Application menu items
// ----------------------------------------------------------------------------
{
    MI_DB48_SETTINGS,           // Application setting
    MI_DB48_ABOUT,              // About dialog

    MI_48PGM,                   // File operations on programs
    MI_48STATE,                 // File operations on state

    MI_MSC,                     // Activate USB disk
    MI_PGM_LOAD,                // Load program
    MI_LOAD_QSPI,               // Load QSPI
    MI_SYSTEM_ENTER,            // Enter system
    0
}; // Terminator


const smenu_t application_menu =
// ----------------------------------------------------------------------------
//   Application menu
// ----------------------------------------------------------------------------
{
    "Setup",  application_menu_items,   NULL, NULL
};


void about_dialog()
// ----------------------------------------------------------------------------
//   Display the About dialog
// ----------------------------------------------------------------------------
{
  lcd_clear_buf();
  lcd_writeClr(t24);

  // Header based on original system about
  lcd_for_calc(DISP_ABOUT);
  lcd_putsAt(t24,4,"");
  lcd_prevLn(t24);

  // Display the main text
  int h2 = lcd_lineHeight(t20)/2; // Extra spacing
  lcd_setXY(t20, t24->x, t24->y + h2);
  lcd_puts(t20, "DB48X v" PROGRAM_VERSION " (C) C. de Dinechin");
  t20->y += h2;
  lcd_puts(t20, "DMCP platform (C) SwissMicros GmbH");
  lcd_puts(t20, "Intel Decimal Floating Point Library v2.0u1");
  lcd_puts(t20, "  (C) 2007-2018, Intel Corp.");

  t20->y = LCD_Y - lcd_lineHeight(t20);
  lcd_putsR(t20, "    Press EXIT key to continue...");

  lcd_refresh();

  wait_for_key_press();
}



// ============================================================================
//
//    Settings menu
//
// ============================================================================

const uint8_t settings_menu_items[] =
// ----------------------------------------------------------------------------
//    Settings menu items
// ----------------------------------------------------------------------------
{
    MI_SET_TIME,                // Standard set time menu
    MI_SET_DATE,                // Standard set date menu
    MI_BEEP_MUTE,               // Mute the beep
    MI_SLOW_AUTOREP,            // Slow auto-repeat
    0
}; // Terminator


const smenu_t settings_menu =
// ----------------------------------------------------------------------------
//   Settings menu
// ----------------------------------------------------------------------------
{
    "Settings",  settings_menu_items,  NULL, NULL
};



// ============================================================================
//
//    Program load/save menu
//
// ============================================================================

const uint8_t program_menu_items[] =
// ----------------------------------------------------------------------------
//    Program menu items
// ----------------------------------------------------------------------------
{
    MI_48PGM_LOAD,              // Load a 48 program from disk
    MI_48PGM_SAVE,              // Save a 48 program to disk
    MI_MSC,                     // Activate USB disk
    MI_DISK_INFO,               // Show disk information

    0
}; // Terminator


const smenu_t program_menu =
// ----------------------------------------------------------------------------
//   Program menu
// ----------------------------------------------------------------------------
{
    "Program",  program_menu_items,  NULL, NULL
};


static int program_save_callback(const char *fpath,
                                 const char *fname,
                                 void       *data)
// ----------------------------------------------------------------------------
//   Callback when a file is selected to save a program
// ----------------------------------------------------------------------------
{
    // Display the name of the file being saved
    lcd_puts(t24,"Saving program...");
    lcd_puts(t24, fname);
    lcd_refresh();

    // Store the state file name
    file prog(fpath);
    if (!prog.valid())
    {
        disp_disk_info("Program save");
        return 1;
    }

    renderer render(&prog);
    render.put("Hello World!\n");

    // Exit with success
    return 0;
}


static int program_save()
// ----------------------------------------------------------------------------
//   Save a program to disk
// ----------------------------------------------------------------------------
{
    // Check if we have enough power to write flash disk
    if (power_check_screen())
        return 0;

    bool display_new = true;
    bool overwrite_check = true;
    void *user_data = NULL;
    int ret = file_selection_screen("Save program",
                                    "/PROGRAMS", ".48S",
                                    program_save_callback,
                                    display_new, overwrite_check,
                                    user_data);
    return ret;
}


static int program_load_callback(const char *fpath,
                                 const char *fname,
                                 void       *data)
// ----------------------------------------------------------------------------
//   Callback when a file is selected for loading
// ----------------------------------------------------------------------------
{
    // Display the name of the file being saved
    lcd_puts(t24,"Loading program...");
    lcd_puts(t24, fname);
    lcd_refresh();

    // Store the state file name
    file prog;
    prog.open(fpath);
    if (!prog.valid())
    {
        disp_disk_info("Program load");
        return 1;
    }

    // Exit with success
    return 0;
}


static int program_load()
// ----------------------------------------------------------------------------
//   Load a program from disk
// ----------------------------------------------------------------------------
{
    bool display_new = false;
    bool overwrite_check = true;
    void *user_data = NULL;
    int ret = file_selection_screen("Load program",
                                    "/PROGRAMS", ".48S",
                                    program_load_callback,
                                    display_new, overwrite_check,
                                    user_data);
    return ret;
}


const uint8_t state_menu_items[] =
// ----------------------------------------------------------------------------
//    Program menu items
// ----------------------------------------------------------------------------
{
    MI_48STATE_LOAD,            // Load a 48 program from disk
    MI_48STATE_SAVE,            // Save a 48 program to disk
    MI_48STATE_CLEAN,           // Start with a fresh clean state
    MI_MSC,                     // Activate USB disk
    MI_DISK_INFO,               // Show disk information

    0
}; // Terminator


const smenu_t state_menu =
// ----------------------------------------------------------------------------
//   Program menu
// ----------------------------------------------------------------------------
{
    "State",  state_menu_items,  NULL, NULL
};


static int state_save_callback(const char *fpath,
                                 const char *fname,
                                 void       *data)
// ----------------------------------------------------------------------------
//   Callback when a file is selected
// ----------------------------------------------------------------------------
{
    // Display the name of the file being saved
    lcd_puts(t24,"Saving state...");
    lcd_puts(t24, fname);
    lcd_refresh();

    // Store the state file name
    set_reset_state_file(fpath);

    // Exit with appropriate code to force statefile save
    return MAGIC_SAVE_STATE;
}


static int state_save()
// ----------------------------------------------------------------------------
//   Save a program to disk
// ----------------------------------------------------------------------------
{
    // Check if we have enough power to write flash disk
    if (power_check_screen())
        return 0;

    bool display_new = true;
    bool overwrite_check = true;
    void *user_data = NULL;
    int ret = file_selection_screen("Save state",
                                    "/STATE", ".48S",
                                    state_save_callback,
                                    display_new, overwrite_check,
                                    user_data);
    return ret;
}



static int state_load_callback(const char *fpath,
                                 const char *fname,
                                 void       *data)
// ----------------------------------------------------------------------------
//   Callback when a file is selected for loading
// ----------------------------------------------------------------------------
{
    // Display the name of the file being saved
    lcd_puts(t24,"Loading state...");
    lcd_puts(t24, fname);
    lcd_refresh();

    // Store the state file name
    file prog;
    prog.open(fpath);
    if (!prog.valid())
    {
        disp_disk_info("State load");
        return 1;
    }

    // Exit with success
    return 0;
}


static int state_load()
// ----------------------------------------------------------------------------
//   Load a state from disk
// ----------------------------------------------------------------------------
{
    bool display_new = false;
    bool overwrite_check = true;
    void *user_data = NULL;
    int ret = file_selection_screen("Load state",
                                    "/STATE", ".48S",
                                    state_load_callback,
                                    display_new, overwrite_check,
                                    user_data);
    return ret;
}


static int state_clear()
// ----------------------------------------------------------------------------
//   Reset calculator to factory state
// ----------------------------------------------------------------------------
{
    return 0;
}


int menu_item_run(uint8_t menu_id)
// ----------------------------------------------------------------------------
//   Callback to run a menu item
// ----------------------------------------------------------------------------
{
  int ret = 0;

  switch(menu_id)
  {
  case MI_DB48_ABOUT:    about_dialog(); break;
  case MI_DB48_SETTINGS: ret = handle_menu(&settings_menu, MENU_ADD, 0); break;
  case MI_48PGM:         ret = handle_menu(&program_menu, MENU_ADD, 0); break;
  case MI_48PGM_LOAD:    ret = program_load(); break;
  case MI_48PGM_SAVE:    ret = program_save(); break;
  case MI_48STATE:       ret = handle_menu(&state_menu, MENU_ADD, 0); break;
  case MI_48STATE_LOAD:  ret = state_load(); break;
  case MI_48STATE_SAVE:  ret = state_save(); break;
  case MI_48STATE_CLEAN: ret = state_clear(); break;
  default:               ret = MRET_UNIMPL; break;
  }

  return ret;
}


cstring menu_item_description(uint8_t          menu_id,
                              char *UNUSED     s,
                              const int UNUSED len)
// ----------------------------------------------------------------------------
//   Return the menu item description
// ----------------------------------------------------------------------------
{
    const char *ln;

    switch (menu_id)
    {
    case MI_DB48_SETTINGS:      ln = "Settings >";      break;
    case MI_DB48_ABOUT:         ln = "About >";         break;
    case MI_48PGM:              ln = "Program >";       break;
    case MI_48PGM_LOAD:         ln = "Load Program";    break;
    case MI_48PGM_SAVE:         ln = "Save Program";    break;
    case MI_48STATE:            ln = "State >";         break;
    case MI_48STATE_LOAD:       ln = "Load State";      break;
    case MI_48STATE_SAVE:       ln = "Save State";      break;
    case MI_48STATE_CLEAN:      ln = "Clear state";     break;
    default:                    ln = NULL;              break;
    }

    return ln;
}
