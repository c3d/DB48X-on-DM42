// ****************************************************************************
//  screen.cpp                                                    DB48X project
// ****************************************************************************
//
//   File Description:
//
//
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

#include "sim-screen.h"

#include "dmcp.h"
#include "sim-dmcp.h"
#include <target.h>


// A copy of the LCD buffer
pixword lcd_copy[sizeof(lcd_buffer) / sizeof(*lcd_buffer)];

#ifdef WASM

uintptr_t ui_update_pixmap()
// ----------------------------------------------------------------------------
//   Recompute the pixmap
// ----------------------------------------------------------------------------
//   This should be done on the RPL thread to get a consistent picture
{
    // Monochrome screen
    pixword mask = ~(~0U << color::BPP);
    surface s(lcd_buffer, LCD_W, LCD_H, LCD_SCANLINE);
    for (int y = 0; y < SIM_LCD_H; y++)
    {
        for (int xw = 0; xw < SIM_LCD_SCANLINE*color::BPP/32; xw++)
        {
            unsigned woffs = y * (SIM_LCD_SCANLINE*color::BPP/32) + xw;
            if (uint32_t diffs = lcd_copy[woffs] ^ lcd_buffer[woffs])
                lcd_copy[woffs] = lcd_buffer[woffs];
        }
    }
    return uintptr_t(lcd_buffer);
}



#else // Qt simulator

#include <QBitmap>
#include <QGraphicsPixmapItem>
#include <QTimer>

SimScreen *SimScreen::theScreen = nullptr;

SimScreen::SimScreen(QWidget *parent)
// ----------------------------------------------------------------------------
//   Initialize the screen
// ----------------------------------------------------------------------------
    : QGraphicsView(parent),
      screen_width(SIM_LCD_W),
      screen_height(SIM_LCD_H),
      scale(1),
#ifndef CONFIG_COLOR
      bgColor(230, 230, 230),
#else // CONFIG_COLOR
      bgColor(255, 255, 255),
#endif // CONFIG_COLOR
      fgColor(0, 0, 0),
      bgPen(bgColor),
      fgPen(fgColor),
      mainPixmap(SIM_LCD_W, SIM_LCD_H),
      redraws(0)
{
    screen.clear();
    screen.setBackgroundBrush(QBrush(Qt::black));

    mainPixmap.fill(bgColor);
    mainScreen = screen.addPixmap(mainPixmap);
    mainScreen->setOffset(0.0, 0.0);

    setScene(&screen);
    setSceneRect(0, -5, screen_width, screen_height + 5);
    centerOn(qreal(screen_width) / 2, qreal(screen_height) / 2);
    scale = 1.0;
    setScale(4.0);

    for (size_t i = 0; i < sizeof(lcd_copy) / sizeof(*lcd_copy); i++)
        lcd_copy[i] = ~lcd_buffer[i];

    show();

    theScreen = this;
}


SimScreen::~SimScreen()
// ----------------------------------------------------------------------------
//   SimScreen destructor
// ----------------------------------------------------------------------------
{
}


void SimScreen::setScale(qreal sf)
// ----------------------------------------------------------------------------
//   Adjust the scaling factor
// ----------------------------------------------------------------------------
{
    QGraphicsView::scale(sf / scale, sf / scale);
    this->scale = sf;

    QSize s;
    s.setWidth(0);
    s.setHeight((screen_height + 5) * scale);
    setMinimumSize(s);
}


void SimScreen::updatePixmap()
// ----------------------------------------------------------------------------
//   Recompute the pixmap
// ----------------------------------------------------------------------------
//   This should be done on the RPL thread to get a consistent picture
{
    // Monochrome screen
    QPainter pt(&mainPixmap);
    pixword mask = ~(~0U << color::BPP);
    surface s(lcd_buffer, LCD_W, LCD_H, LCD_SCANLINE);
    for (int y = 0; y < SIM_LCD_H; y++)
    {
        for (int xw = 0; xw < SIM_LCD_SCANLINE*color::BPP/32; xw++)
        {
            unsigned woffs = y * (SIM_LCD_SCANLINE*color::BPP/32) + xw;
            if (uint32_t diffs = lcd_copy[woffs] ^ lcd_buffer[woffs])
            {
                for (int bit = 0; bit < 32; bit += color::BPP)
                {
                    if ((diffs >> bit) & mask)
                    {
                        pixword bits = (lcd_buffer[woffs] >> bit) & mask;
                        color col(bits);
#ifdef CONFIG_COLOR
                        QColor qcol(col.red(), col.green(), col.blue());
#else
                        QColor &qcol = bits ? bgColor : fgColor;
#endif
                        pt.setPen(qcol);

                        coord xx = (xw * 32 + bit) / color::BPP;
                        coord yy = y;
                        s.horizontal_adjust(xx, xx);
                        s.vertical_adjust(yy, yy);
                        pt.drawPoint(xx, yy);
                    }
                }
                lcd_copy[woffs] = lcd_buffer[woffs];
            }
        }
    }
    pt.end();
}


void SimScreen::refreshScreen()
// ----------------------------------------------------------------------------
//   Transfer the pixmap to the screen
// ----------------------------------------------------------------------------
//   This must be done on the main screen
{
    mainScreen->setPixmap(mainPixmap);
    QGraphicsView::update();
    redraws++;
}
#endif // WASM
