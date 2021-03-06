/*
*   This file is part of Luma3DS
*   Copyright (C) 2016-2019 Aurora Wright, TuxSH
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*   Additional Terms 7.b and 7.c of GPLv3 apply to this file:
*       * Requiring preservation of specified reasonable legal notices or
*         author attributions in that material or in the Appropriate Legal
*         Notices displayed by works containing it.
*       * Prohibiting misrepresentation of the origin of that material,
*         or requiring that modified versions of such material be marked in
*         reasonable ways as different from the original version.
*/

#include <3ds.h>
#include "memory.h"
#include "services.h"
#include "fsreg.h"
#include "menu.h"
#include "service_manager.h"
#include "errdisp.h"
#include "hbloader.h"
#include "3dsx.h"
#include "utils.h"
#include "MyThread.h"
#include "menus/process_patches.h"
#include "menus/miscellaneous.h"
#include "menus/screen_filters.h"
#include "shell_open.h"

// this is called before main
bool isN3DS;
void __appInit()
{
    srvSysInit();
    fsregInit();

    fsSysInit();
}

// this is called after main exits
void __appExit()
{
    fsExit();
    fsregExit();
    srvSysExit();
}


Result __sync_init(void);
Result __sync_fini(void);
void __libc_init_array(void);
void __libc_fini_array(void);

void __ctru_exit()
{
    __libc_fini_array();
    __appExit();
    __sync_fini();
    svcExitProcess();
}


void initSystem()
{
    s64 out;
    isN3DS = svcGetSystemInfo(&out, 0x10001, 0) == 0;

    svcGetSystemInfo(&out, 0x10000, 0x100);
    HBLDR_3DSX_TID = out == 0 ? HBLDR_DEFAULT_3DSX_TID : (u64)out;

    svcGetSystemInfo(&out, 0x10000, 0x101);
    menuCombo = out == 0 ? DEFAULT_MENU_COMBO : (u32)out;

    miscellaneousMenu.items[0].title = HBLDR_3DSX_TID == HBLDR_DEFAULT_3DSX_TID ? "Switch the hb. title to the current app." :
                                                                                  "Switch the hb. title to hblauncher_loader";

    ProcessPatchesMenu_PatchUnpatchFSDirectly();
    __sync_init();
    __appInit();
    __libc_init_array();

    // ROSALINA HACKJOB BEGIN
    // NORMAL APPS SHOULD NOT DO THIS, EVER
    u32 *tls = (u32 *)getThreadLocalStorage();
    memset(tls, 0, 0x80);
    tls[0] = 0x21545624;
    // ROSALINA HACKJOB END

    // Rosalina specific:
    srvSetBlockingPolicy(true); // GetServiceHandle nonblocking if service port is full
}

bool terminationRequest = false;
Handle terminationRequestEvent;

static void handleTermNotification(u32 notificationId)
{
    (void)notificationId;
    // Termination request
    terminationRequest = true;
    svcSignalEvent(terminationRequestEvent);
}

static const ServiceManagerServiceEntry services[] = {
    { "err:f",  1, ERRF_HandleCommands,  true },
    { "hb:ldr", 2, HBLDR_HandleCommands, true },
    { NULL },
};

static const ServiceManagerNotificationEntry notifications[] = {
    { 0x100, handleTermNotification },
    { 0x000, NULL },
};

int main(void)
{
    static u8 ipcBuf[0x100] = {0};  // used by both err:f and hb:ldr

    // Set up static buffers for IPC
    u32* bufPtrs = getThreadStaticBuffers();
    memset(bufPtrs, 0, 16 * 2 * 4);
    bufPtrs[0] = IPC_Desc_StaticBuffer(sizeof(ipcBuf), 0);
    bufPtrs[1] = (u32)ipcBuf;
    bufPtrs[2] = IPC_Desc_StaticBuffer(sizeof(ldrArgvBuf), 1);
    bufPtrs[3] = (u32)ldrArgvBuf;

    if(R_FAILED(svcCreateEvent(&terminationRequestEvent, RESET_STICKY)))
        svcBreak(USERBREAK_ASSERT);

    MyThread *menuThread = menuCreateThread();
    MyThread *shellOpenThread = shellOpenCreateThread();

    if (R_FAILED(ServiceManager_Run(services, notifications, NULL)))
        svcBreak(USERBREAK_PANIC);

    MyThread_Join(menuThread, -1LL);
    MyThread_Join(shellOpenThread, -1LL);

    return 0;
}
