#ifndef MENU_H
#define MENU_H
// ****************************************************************************
//  menu.h                                                        DB48X project
// ****************************************************************************
//
//   File Description:
//
//     An RPL menu object defines the content of the soft menu keys
//
//     It is a catalog which, when evaluated, updates the soft menu keys
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

#include "variables.h"
#include "symbol.h"
#include "input.h"


struct menu : command
// ----------------------------------------------------------------------------
//   An RPL menu object, can define menu keys
// ----------------------------------------------------------------------------
{
    // Constructor
    menu(id type = ID_menu) : command(type) {}

    // Info returned from the MENU opcode
    struct info
    {
        uint    page;                           // In:  Page index
        uint    skip;                           // Int: Items to skip
        uint    pages;                          // Out: Total number of pages
        uint    index;                          // Out: Last index written
        uint    plane;                          // Out: Last plane filled
        uint    planes;                         // Out: Planes the menu wants
    };

    result update(uint page = 0) const
    {
        info mi = { .page = page };
        return (result) run(MENU, RT, &mi);
    }

    static void items_init(info &mi, uint nitems, uint planes = 2);
    static void items(info &UNUSED mi) { }
    static void items(info &mi, cstring label, id action);
    template <typename ... Args>
    static void items(info &mi, cstring label, id action, Args ...args);

    static uint count()         { return 0; }
    template<typename ... Args>
    static uint count(cstring UNUSED label, id UNUSED action, Args ... args)
    {
        return 1 + count(args...);
    }

public:
    OBJECT_HANDLER(menu);
};


template <typename ... Args>
void menu::items(info &mi, cstring label, id type, Args... args)
// ----------------------------------------------------------------------------
//   Update menu items
// ----------------------------------------------------------------------------
{
    items(mi, label, type);
    items(mi, args...);
}


// ============================================================================
//
//   Commands inserted in menus
//
// ============================================================================

COMMAND(MenuNextPage)
// ----------------------------------------------------------------------------
//   Select the next page in the menu
// ----------------------------------------------------------------------------
{
    Input.page(Input.page() + 1);
    return OK;
}


COMMAND(MenuPreviousPage)
// ----------------------------------------------------------------------------
//   Select the previous page in the menu
// ----------------------------------------------------------------------------
{
    Input.page((Input.page() - 1) % Input.pages());
    return OK;
}


COMMAND(MenuFirstPage)
// ----------------------------------------------------------------------------
//   Select the previous page in the menu
// ----------------------------------------------------------------------------
{
    Input.page(0);
    return OK;
}



// ============================================================================
//
//   Creation of a menu
//
// ============================================================================

#define MENU(SysMenu, ...)                                              \
struct SysMenu : menu                                                   \
/* ------------------------------------------------------------ */      \
/*   Create a system menu                                       */      \
/* ------------------------------------------------------------ */      \
{                                                                       \
    SysMenu(id type = ID_##SysMenu) : menu(type) {}                     \
                                                                        \
    OBJECT_HANDLER(SysMenu)                                             \
    {                                                                   \
        switch(op)                                                      \
        {                                                               \
        case MENU:                                                      \
        {                                                               \
            info &mi   = *((info *) arg);                               \
            uint nitems = count(__VA_ARGS__);                           \
            items_init(mi, nitems, 2);                                  \
            items(mi, ## __VA_ARGS__);                                  \
            return OK;                                                  \
        }                                                               \
        default:                                                        \
            return DELEGATE(menu);                                      \
        }                                                               \
    }                                                                   \
}



// ============================================================================
//
//    Menu hierarchy
//
// ============================================================================

MENU(MainMenu,
     "Math",            ID_MathMenu,
     "Program",         ID_ProgramMenu);

MENU(MathMenu,
     "Real",            ID_RealMenu,
     "Complex",         ID_ComplexMenu,
     "Bases",           ID_BasesMenu,
     "Vector",          ID_VectorMenu,
     "Matrix",          ID_MatrixMenu,
     "Constants",       ID_ConstantsMenu,

     "Hyperbolic",      ID_HyperbolicMenu,
     "Probabilities",   ID_ProbabilitiesMenu,
     "Statistics",      ID_StatisticsMenu,
     "Fourier",         ID_FourierMenu,
     "Symbolic",        ID_SymbolicMenu);

MENU(RealMenu,
     "Circular",        ID_CircularMenu,
     "Hyperbolic",      ID_HyperbolicMenu);

MENU(ComplexMenu,
     "→ℂ",              ID_unimplemented,
     "𝒊",               ID_unimplemented,
     "𝒋",               ID_unimplemented,
     "𝒌",               ID_unimplemented);

MENU(VectorMenu);
MENU(MatrixMenu);
MENU(HyperbolicMenu);
MENU(CircularMenu);
MENU(BasesMenu);
MENU(ProbabilitiesMenu);
MENU(StatisticsMenu);
MENU(FourierMenu);
MENU(ConstantsMenu);
MENU(SymbolicMenu);

MENU(ProgramMenu);
MENU(TestsMenu);
MENU(LoopsMenu);
MENU(ListMenu);

#endif // MENU_H
