/*
    Copyright 2015-2024 Clément Gallet <clement.gallet@ens-lyon.org>

    This file is part of libTAS.

    libTAS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    libTAS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with libTAS.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "GameThread.h"
#include "AutoDetect.h"
#include "utils.h"
#include "Context.h"
#include "../shared/SharedConfig.h"

#include <string>
#include <sstream>
#include <iostream>
#include <unistd.h> // chdir()
#include <fcntl.h> // O_RDWR, O_CREAT

void GameThread::set_env_variables(Context *context, int gameArch)
{
    /* Not interested in macos flag */
    gameArch &= BT_TYPEMASK;

#ifdef __unix__
    /* Update the LD_LIBRARY_PATH environment variable */
    std::ostringstream oss_lib;
    if (gameArch == BT_ELF32)
        oss_lib << context->config.extralib32dir;
    else if (gameArch == BT_ELF64)
        oss_lib << context->config.extralib64dir;
    
    if (!context->config.libdir.empty())
        oss_lib << ":" << context->config.libdir;

    char* oldlibpath = getenv("LD_LIBRARY_PATH");
    if (oldlibpath)
        oss_lib << ":" << oldlibpath;
    
    setenv("LD_LIBRARY_PATH", oss_lib.str().c_str(), 1);
#endif

    /* Change the working directory to the user-defined one or game directory */
    std::string newdir = context->config.rundir;
    if (newdir.empty())
        newdir = dirFromPath(context->gameexecutable);

    if (0 != chdir(newdir.c_str())) {
        std::cerr << "Could not change the working directory to " << newdir << std::endl;
    }

    /* Set PWD environment variable because games may use it and chdir
     * does not update it.
     */
    setenv("PWD", newdir.c_str(), 1);

    /* Set additional environment variables regarding Mesa and VDPAU configurations */
    if (context->config.sc.opengl_soft) {
        setenv("__GLX_VENDOR_LIBRARY_NAME", "mesa", 1);
        setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
        setenv("VK_DRIVER_FILES", "/usr/share/vulkan/icd.d/lvp_icd.i686.json:/usr/share/vulkan/icd.d/lvp_icd.x86_64.json:/usr/share/vulkan/icd.d/lvp_icd.json", 1);
        setenv("VDPAU_DRIVER", "va_gl", 1);
        setenv("VDPAU_QUIRKS", "AvoidVA", 1);
    }

    /* If we prefer OpenAL Soft, we'll want to force it to use either SDL2 or ALSA internally */
    if (context->config.sc.openal_soft) {
        setenv("ALSOFT_DRIVERS", "sdl2,alsa", 1);
    }

    /* Disable Wayland support */
    setenv("WAYLAND_DISPLAY", "", 1);

    /* Pass libtas library path to the game */
    setenv("LIBTAS_LIBRARY_PATH", context->libtaspath.c_str(), 1);

    setenv("LIBTAS_START_FRAME", std::to_string(context->framecount).c_str(), 1);

    /* Override timezone for determinism */
    setenv("TZ", "UTC0", 1);

    /* Set wine-specific env variables */
    if ((gameArch == BT_PE32) || (gameArch == BT_PE32P) || (gameArch == BT_NE)) {

        /* Set specific env variables for Proton */
        if (context->config.use_proton && !context->config.proton_path.empty()) {
            std::string winedllpath = context->config.proton_path;
            winedllpath += "/dist/lib64/wine:";
            winedllpath += context->config.proton_path;
            winedllpath += "/dist/lib/wine";
            setenv("WINEDLLPATH", winedllpath.c_str(), 1);

            char* oldlibpath = getenv("LD_LIBRARY_PATH");
            std::string libpath = context->config.proton_path;
            libpath += "/dist/lib64/:";
            libpath += context->config.proton_path;
            libpath += "/dist/lib/";
            if (oldlibpath) {
                libpath.append(":");
                libpath.append(oldlibpath);
            }
            setenv("LD_LIBRARY_PATH", libpath.c_str(), 1);

            std::string wineprefix = context->config.proton_path;
            wineprefix += "/dist/share/default_pfx/";
            setenv("WINEPREFIX", wineprefix.c_str(), 1);
        }

        /* We need to delay libtas hooking for wine process. */
        setenv("LIBTAS_DELAY_INIT", "1", 1);
    }
    else {
        /* Tell SDL >= 2.0.2 to let us override functions even if it is statically linked.
         * Does not work for wine games, because our custom SDL functions don't
         * have the correct calling convention. */
        setenv("SDL_DYNAMIC_API", context->libtaspath.c_str(), 1);
    }
}

std::list<std::string> GameThread::build_arg_list(Context *context, int gameArch)
{
    bool macappflag = gameArch & BT_MACOSAPP;
    gameArch &= BT_TYPEMASK;

    /* Build the argument list to be fed to execv */
    std::list<std::string> arg_list;

    if ((gameArch == BT_PE32) || (gameArch == BT_PE32P) || (gameArch == BT_NE)) {
        if (context->config.use_proton && !context->config.proton_path.empty()) {
            /* Change the executable to proton */
            std::string winepath = context->config.proton_path;
            winepath += "/dist/bin/wine";
            if (gameArch == BT_PE32P)
                winepath += "64";
            arg_list.push_back(winepath);
        }
        else {
            /* Change the executable to wine */
            std::string winename = "wine";
            if (gameArch == BT_PE32P)
                winename += "64";

            /* wine[64] presence was already checked in ui/ErrorChecking.cpp */
            std::string cmd = "which ";
            cmd += winename;
            
            std::string winepath = queryCmd(cmd);
            arg_list.push_back(winepath);
        }

        /* Push the game executable as the first command-line argument */
        /* Wine can fail if not specifying a Windows path */
        context->gameexecutable.insert(0, "Z:");
        arg_list.push_back(context->gameexecutable);
    }
    else {
        if (context->attach_gdb) {
            std::string cmd;

            switch (context->config.debugger) {
            case Config::DEBUGGER_GDB:
                cmd = "which gdb";
                break;
            case Config::DEBUGGER_LLDB:
                cmd = "which lldb";
                break;
            case Config::DEBUGGER_STRACE:
                cmd = "which strace";
                break;
            }

            std::string dbgpath = queryCmd(cmd);
            arg_list.push_back(dbgpath);

            std::string ldpreloadstr = context->libtaspath;
            if (!context->old_ld_preload.empty()) {
                ldpreloadstr += ":";
                ldpreloadstr += context->old_ld_preload;
            }

            /* Push debugger arguments */
            switch (context->config.debugger) {
            case Config::DEBUGGER_GDB: {
                arg_list.push_back("-q");
                arg_list.push_back("-ex");

                /* LD_PRELOAD must be set inside a gdb
                 * command to be effective */
                ldpreloadstr.insert(0, "set exec-wrapper env 'LD_PRELOAD=");
                ldpreloadstr += "'";
                arg_list.push_back(ldpreloadstr);

                /* We are using SIGSYS and SIGXFSZ for savestates, so don't
                 * print and pause when one signal is sent *
                 * Signals SIGPWR SIGXCPU SIG35 and SIG36 are used a lot in some games */
                arg_list.push_back("-ex");
                arg_list.push_back("handle SIGSYS SIGXFSZ SIGUSR1 SIGUSR2 SIGPWR SIGXCPU SIG34 SIG35 SIG36 nostop noprint");
                arg_list.push_back("-ex");
                arg_list.push_back("run");
                arg_list.push_back("--args");

                break;
            }
            case Config::DEBUGGER_LLDB: {
                arg_list.push_back("-o");

                /* LD_PRELOAD/DYLD_INSERT_LIBRARIES must be set inside an lldb
                 * command to be effective */
#ifdef __unix__
                ldpreloadstr.insert(0, "set se target.env-vars 'LD_PRELOAD=");
#elif defined(__APPLE__) && defined(__MACH__)
                ldpreloadstr.insert(0, "set se target.env-vars 'DYLD_INSERT_LIBRARIES=");
#endif
                ldpreloadstr += "'";
                arg_list.push_back(ldpreloadstr);

#if defined(__APPLE__) && defined(__MACH__)
                arg_list.push_back("-o");
                arg_list.push_back("set se target.env-vars 'DYLD_FORCE_FLAT_NAMESPACE=1'");
#endif

                /* We are using SIGSYS and SIGXFSZ for savestates, so don't
                 * print and pause when one signal is sent */
                arg_list.push_back("-o");
                arg_list.push_back("run");
                /* Signal handling cannot be performed in llvm before the process has started */
//                arg_list.push_back("-o");
//                arg_list.push_back("process handle -n false -p false -s false SIGSYS SIGXFSZ SIGUSR1 SIGUSR2 SIGXCPU");
                arg_list.push_back("--");

                break;
            }
            case Config::DEBUGGER_STRACE: {
                arg_list.push_back("-f"); // trace all threads

                /* LD_PRELOAD must be set inside as strace argument */
                arg_list.push_back("-E");
                ldpreloadstr.insert(0, "LD_PRELOAD=");
                arg_list.push_back(ldpreloadstr);

                if (!context->config.strace_events.empty()) {
                    arg_list.push_back("-e");
                    arg_list.push_back(context->config.strace_events);
                }
                break;
            }
            }
        }

        /* If MacOS app, insert the real executable */
        if (macappflag) {
            arg_list.push_back(extractMacOSExecutable(context->gameexecutable));
        }
        else {
            arg_list.push_back(context->gameexecutable);
        }
    }

    return arg_list;
}

void GameThread::launch(Context *context)
{
    /* Detect the game executable arch and handle 32-bit game on 64-bit arch case */
    int gameArch = AutoDetect::arch(context);

    /* Set all environment variables */
    set_env_variables(context, gameArch);
    
    /* Tries to detect a library folder within the game directory */
#ifdef __unix__
    AutoDetect::game_libraries(context);
#endif

    /* Set where stderr of the game is redirected */
    int fd;
    std::string logfile = context->gamepath + ".log";
    switch(context->config.sc.logging_status) {
        case SharedConfig::NO_LOGGING:
            fd = open("/dev/null", O_RDWR, S_IRUSR | S_IWUSR);
            dup2(fd, 2);
            close(fd);
            break;
        case SharedConfig::LOGGING_TO_FILE:
            std::cout << "Logging to file: " << logfile << std::endl;
            fd = open(logfile.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
            dup2(fd, 2);
            close(fd);
            break;
        case SharedConfig::LOGGING_TO_CONSOLE:
        default:
            break;
    }

    /* Build the argument list for running the game */
    std::list<std::string> arg_list = build_arg_list(context, gameArch);

    /* Argument string for sh */
    std::ostringstream sharg;

    /* Prepend LD_PRELOAD/DYLD_INSERT_LIBRARIES */
    if (!(context->attach_gdb && (!(((gameArch&BT_TYPEMASK) == BT_PE32) || ((gameArch&BT_TYPEMASK) == BT_PE32P) || ((gameArch&BT_TYPEMASK) == BT_NE))))) {
        /* Set the LD_PRELOAD/DYLD_INSERT_LIBRARIES environment variable to
         * inject our lib to the game */
#ifdef __unix__
        sharg << "LD_PRELOAD=";
#elif defined(__APPLE__) && defined(__MACH__)
        sharg << "DYLD_INSERT_LIBRARIES=";
#endif
        if (!context->old_ld_preload.empty()) {
            sharg << context->libtaspath << ":" << context->old_ld_preload << " ";
        }
        else {
            sharg << context->libtaspath << " ";
        }
        
        /* We need to set DYLD_FORCE_FLAT_NAMESPACE so that we can hook into the game */
#if defined(__APPLE__) && defined(__MACH__)
        sharg << "DYLD_FORCE_FLAT_NAMESPACE=1 ";
#endif
    }

    /* Escape and concatenate arguments */
    for (std::string arg : arg_list) {
        /* Replace all occurrences of `'` with `'\'''` */
        const std::string escape_string = "'\\''";
        size_t pos = arg.find("'");
        while(pos != std::string::npos) {
            arg.replace(pos, 1, escape_string);
            pos = arg.find("'", pos + escape_string.size());
        }

        /* Add to the argument string with enclosed `'` and space */
        sharg << "'" << arg << "' ";
    }

    /* Append the game command-line arguments */
    sharg << context->config.gameargs;

    /* Run the actual game with sh, taking care of splitting arguments */
    execlp("sh", "sh", "-c", sharg.str().c_str(), nullptr);
}

void GameThread::attach(Context *context)
{
    switch (context->config.debugger) {
        case Config::DEBUGGER_GDB: {
            execlp("gdb", "gdb", "-q", "-ex",
                   "handle SIGSYS SIGXFSZ SIGUSR1 SIGUSR2 SIGPWR SIGXCPU SIG34 SIG35 SIG36 nostop noprint",
                   context->gameexecutable.c_str(), "-p", std::to_string(context->game_pid).c_str(), (char *) NULL);
            break;
        }
        case Config::DEBUGGER_LLDB: {
            execlp("lldb", "lldb",
                   "-p", std::to_string(context->game_pid).c_str(), (char *) NULL);
            break;
        }
    }
}
