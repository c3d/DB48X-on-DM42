// ****************************************************************************
//  main.cpp                                                      DB48X project
// ****************************************************************************
//
//   File Description:
//
//     DM42 simulator for the DB48 project
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

#include "main.h"
#include "object.h"
#include "recorder.h"
#include "settings.h"
#include "sim-rpl.h"
#include "sim-window.h"
#include "sysmenu.h"

#include <unistd.h>

#include <QApplication>
#include <QWindow>
#include <QStandardPaths>
#include <QDirIterator>

RECORDER(options, 32, "Information about command line options");
RECORDER_TWEAK_DEFINE(rpl_objects_detail, 0, "Set to 1 to see object addresses")

bool   run_tests   = false;
bool   noisy_tests = false;
bool   no_beep     = false;
uint   memory_size = 100; // Memory size in kilobytes

size_t recorder_render_object(intptr_t tracing,
                              const char *UNUSED /* format */,
                              char *buffer, size_t size,
                              uintptr_t arg)
// ----------------------------------------------------------------------------
//   Render a value during a recorder dump (%t format)
// ----------------------------------------------------------------------------
{
    object_p value = object_p(arg);
    size_t result = 0;
    if (tracing)
    {
        if (value)
        {
            char tmp[80];
            size_t sz =  value->render(tmp, sizeof(tmp)-1);
            if (sz >= sizeof(tmp))
                sz = sizeof(tmp)-1;
            tmp[sz] = 0;
            if (RECORDER_TWEAK(rpl_objects_detail))
                result = snprintf(buffer, size, "%p[%lu] %s[%s]",
                                  (void *) value,
                                  value->size(),
                                  value->fancy(),
                                  tmp);
            else
                result = snprintf(buffer, size, "%s", tmp);

        }
        else
        {
            result = snprintf(buffer, size, "0x0 <NULL>");
        }
    }
    else
    {
        result = snprintf(buffer, size, "%p", (void *) value);
    }
    return result;
}


// Ensure linker keeps debug code
extern cstring debug();

void copyAndReplaceFolderContents(const QString &fromDir, const QString &toDir) {
    QDirIterator it(fromDir, QDirIterator::Subdirectories);
    QDir from(fromDir);
    QDir to(toDir);

    while (it.hasNext()){
        it.next();
        const auto fileInfo = it.fileInfo();
        if(!fileInfo.isHidden()) { //filters dot and dotdot
            const QString constructedAbsolutePath = to.filePath(from.relativeFilePath(fileInfo.absoluteFilePath()));
            
            if(fileInfo.isDir()){
                //Create directory in target folder
                to.mkpath(constructedAbsolutePath);
            } else if(fileInfo.isFile()) {
                //Copy File to target directory

                //Remove file at target location, if it exists, or QFile::copy will fail
                QFile::remove(constructedAbsolutePath);
                QFile::copy(fileInfo.absoluteFilePath(), constructedAbsolutePath);
            }
        }
    }
}

int main(int argc, char *argv[])
// ----------------------------------------------------------------------------
//   Main entry point for the simulator
// ----------------------------------------------------------------------------
{
    const char *traces = getenv("DB48X_TRACES");
    recorder_trace_set(".*(error|warn(ing)?)s?");
    if (traces)
        recorder_trace_set(traces);
    recorder_dump_on_common_signals(0, 0);
    recorder_configure_type('t', recorder_render_object);

    // This is just to link otherwise unused code intended for use in debugger
    if (traces && traces[0] == char(0xFF))
        if (cstring result = debug())
            record(options, "Strange input %s", result);

    // Indicate the first two-byte opcode
    fprintf(stderr,
            "%s version %s\n"
            "Last single-byte opcode is %s\n"
            "First two byte opcode is %s\n"
            "Total of %u opcodes\n"
            "Help file name is %s\n",
            PROGRAM_NAME,
            DB48X_VERSION,
            object::name(object::id(127)),
            object::name(object::id(128)),
            uint(object::NUM_IDS),
            HELPFILE_NAME);

    record(options,
           "Simulator invoked as %+s with %d arguments", argv[0], argc-1);
    for (int a = 1; a < argc; a++)
    {
        cstring as = argv[a];
        record(options, "  %u: %+s", a, as);
        if (as[0] == '-')
        {
            switch(as[1])
            {
            case 't':
                recorder_trace_set(as+2);
                break;
            case 'n':
                noisy_tests = true;
                break;
            case 'N':
                no_beep = true;
                break;

            case 'T':
                run_tests = true;
                // fall-through
            case 'O':
                if (as[2])
                {
                    static bool first = true;
                    if (first)
                    {
                        recorder_trace_set("est_.*=0");
                        first = false;
                    }
                    char tname[256];
                    if (strcmp(as+2, "all") == 0)
                        strcpy(tname, "est_.*");
                    else
                        snprintf(tname, sizeof(tname)-1, "est_%s", as+2);
                    recorder_trace_set(tname);
                }
                break;
            case 'D':
                if (as[2])
                    tests::dump_on_fail = as+2;
                else if (a < argc)
                    tests::dump_on_fail = argv[++a];
                break;

            case 'k':
                if (as[2])
                    load_saved_keymap(as + 2);
                else if (a < argc)
                    load_saved_keymap(argv[++a]);
                break;
            case 'w':
                if (as[2])
                    tests::default_wait_time = atoi(as+2);
                else if (a < argc)
                    tests::default_wait_time = atoi(argv[++a]);
                break;
            case 'd':
                if (as[2])
                    tests::key_delay_time = atoi(as+2);
                else if (a < argc)
                    tests::key_delay_time = atoi(argv[++a]);
                break;
            case 'r':
                if (as[2])
                    tests::refresh_delay_time = atoi(as+2);
                else if (a < argc)
                    tests::refresh_delay_time = atoi(argv[++a]);
                break;
            case 'i':
                if (as[2])
                    tests::image_wait_time = atoi(as+2);
                else if (a < argc)
                    tests::image_wait_time = atoi(argv[++a]);
                break;
            case 'm':
                if (as[2])
                    memory_size = atoi(as+2);
                else if (a < argc)
                    memory_size = atoi(argv[++a]);
                break;
            case 's':
                if (as[2])
                    MainWindow::devicePixelRatio = atof(as+2);
                else if (a < argc)
                    MainWindow::devicePixelRatio = atof(argv[++a]);
                break;

            }
        }
        else if (cstring pos = strchr(as, '='))
        {
            size_t len   = pos - as;
            ularge value = strtoull(pos + 1, nullptr, 0);

#define ID(Name)                                                \
            const object::id ID_##Name = object::ID_##Name;     \
            (void) ID_##Name;
#include "ids.tbl"

            if (isalpha(pos[1]))
            {
                if (!strcasecmp(pos+1, "yes") || !strcasecmp(pos+1, "true"))
                    value = true;

#define ID(id)
#define SETTING_ENUM(Name, Alias, Base)         \
                if (sizeof(#Name) - 1 == len && \
                    (!strcasecmp(#Name, pos+1) ||       \
                     !strcasecmp(#Alias, pos+1)))       \
                    value = int(ID_##Name);
#include "ids.tbl"
            }

#define ID(id)
#define SETTING(Name, Low, High, Init)                  \
            if (sizeof(#Name) - 1 == len &&             \
                strncasecmp(#Name, as, len) == 0)       \
                Settings.Name((typeof(Init))value);     \
            else
#define FLAG(Enable, Disable)                           \
            if (sizeof(#Enable) - 1 == len &&           \
                strncasecmp(#Enable, as, len) == 0)     \
                Settings.Enable((bool) value);          \
            else if (sizeof(#Disable) - 1 == len &&     \
                strncasecmp(#Disable, as, len) == 0)    \
                Settings.Disable((bool) value);         \
            else

#include "ids.tbl"
            fprintf(stderr,
                    "Warning: Setting '%.*s' does not exist\n", int(len), as);
        }
    }

#if QT_VERSION < 0x060000
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif // QT version 6


    QCoreApplication::setOrganizationName("DB48X Project");
    QCoreApplication::setOrganizationDomain("48calc.org");
    QCoreApplication::setApplicationName("DB48X");

    QString file_location = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir t_dir(file_location);
    if (!t_dir.exists()) {
        t_dir.mkpath(file_location);
    }

    copyAndReplaceFolderContents(":/", file_location);

    QDir::setCurrent(file_location);

    QApplication a(argc, argv);
    MainWindow w;
    w.show();

    return a.exec();
}
