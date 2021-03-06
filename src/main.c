#include <string.h>
#include <malloc.h>
#include "dynamic_libs/os_functions.h"
#include "dynamic_libs/sys_functions.h"
#include "dynamic_libs/vpad_functions.h"
#include "dynamic_libs/socket_functions.h"
#include "system/memory.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include "common/common.h"

#define TITLE_TEXT                  "WUP installer by crediar (HBL version 1.0 by Dimok)"

#define MCP_COMMAND_INSTALL_ASYNC   0x81
#define MAX_INSTALL_PATH_LENGTH     0x27F

static int installCompleted = 0;
static int installSuccess = 0;
static int installToUsb = 0;
static u32 installError = 0;
static u64 installedTitle = 0;
static u64 baseTitleId = 0;

static void PrintError(const char *errorStr)
{
    for(int i = 0; i < 2; i++)
    {
        OSScreenClearBufferEx(i, 0);
        OSScreenPutFontEx(i, 0, 0, TITLE_TEXT);
        OSScreenPutFontEx(i, 0, 2, errorStr);
        OSScreenFlipBuffersEx(i);
    }
    sleep(4);
}

static int IosInstallCallback(unsigned int errorCode, unsigned int * priv_data)
{
    installError = errorCode;
    installCompleted = 1;
    return 0;
}

static void InstallTitle(const char *titlePath, int ignoreWhitelist)
{
    //!---------------------------------------------------
    //! This part of code originates from Crediars MCP patcher assembly code
    //! it is just translated to C
    //!---------------------------------------------------
    unsigned int mcpHandle = MCP_Open();
    if(mcpHandle == 0)
    {
        PrintError("Failed to open MCP.");
        return;
    }

    char text[50];
    unsigned int * mcpInstallInfo = (unsigned int *)OSAllocFromSystem(0x24, 0x40);
    char * mcpInstallPath = (char *)OSAllocFromSystem(MAX_INSTALL_PATH_LENGTH, 0x40);
    unsigned int * mcpPathInfoVector = (unsigned int *)OSAllocFromSystem(0x0C, 0x40);

    do
    {
        if(!mcpInstallInfo || !mcpInstallPath || !mcpPathInfoVector)
        {
            PrintError("Error: Could not allocate memory.");
            break;
        }

        __os_snprintf(text, sizeof(text), titlePath);

        int result = MCP_InstallGetInfo(mcpHandle, text, mcpInstallInfo);
        if(result != 0)
        {
            __os_snprintf(text, sizeof(text), "Error: MCP_InstallGetInfo 0x%08X", MCP_GetLastRawError());
            PrintError(text);
            break;
        }

        u32 titleIdHigh = mcpInstallInfo[0];
        u32 titleIdLow = mcpInstallInfo[1];

        if(   (titleIdHigh == 0x0005000E)    // game update
           || (titleIdLow == 0x10041000)     // JAP title
           || (titleIdLow == 0x10041100)     // USA title
           || (titleIdLow == 0x10041200)     // EUR title
           || ignoreWhitelist)
        {
            installedTitle = ((u64)titleIdHigh << 32ULL) | titleIdLow;

            if(installToUsb && (ignoreWhitelist || (titleIdHigh == 0x0005000E)))
            {
                result = MCP_InstallSetTargetDevice(mcpHandle, 1);
                if(result != 0)
                {
                    __os_snprintf(text, sizeof(text), "Error: MCP_InstallSetTargetUsb 0x%08X", MCP_GetLastRawError());
                    PrintError(text);
                    break;
                }
                result = MCP_InstallSetTargetUsb(mcpHandle, installToUsb);
                if(result != 0)
                {
                    __os_snprintf(text, sizeof(text), "Error: MCP_InstallSetTargetUsb 0x%08X", MCP_GetLastRawError());
                    PrintError(text);
                    break;
                }
            }

            mcpInstallInfo[2] = (unsigned int)MCP_COMMAND_INSTALL_ASYNC;
            mcpInstallInfo[3] = (unsigned int)mcpPathInfoVector;
            mcpInstallInfo[4] = (unsigned int)1;
            mcpInstallInfo[5] = (unsigned int)0;

            memset(mcpInstallPath, 0, MAX_INSTALL_PATH_LENGTH);
            __os_snprintf(mcpInstallPath, MAX_INSTALL_PATH_LENGTH, titlePath);
            memset(mcpPathInfoVector, 0, 0x0C);

            mcpPathInfoVector[0] = (unsigned int)mcpInstallPath;
            mcpPathInfoVector[1] = (unsigned int)MAX_INSTALL_PATH_LENGTH;

            result = IOS_IoctlvAsync(mcpHandle, MCP_COMMAND_INSTALL_ASYNC, 1, 0, mcpPathInfoVector, IosInstallCallback, mcpInstallInfo);
            if(result != 0)
            {
                __os_snprintf(text, sizeof(text), "Error: MCP_InstallTitleAsync 0x%08X", MCP_GetLastRawError());
                PrintError(text);
                break;
            }

            while(!installCompleted)
            {
                memset(mcpInstallInfo, 0, 0x24);

                result = MCP_InstallGetProgress(mcpHandle, mcpInstallInfo);

                if(mcpInstallInfo[0] == 1)
                {
                    int percent = (mcpInstallInfo[4] != 0) ? ((mcpInstallInfo[6] * 100.0f) / mcpInstallInfo[4]) : 0;
                    for(int i = 0; i < 2; i++)
                    {
                        OSScreenClearBufferEx(i, 0);

                        OSScreenPutFontEx(i, 0, 0, TITLE_TEXT);
                        OSScreenPutFontEx(i, 0, 2, "Installing title...");

                        __os_snprintf(text, sizeof(text), "%08X%08X - %0.1f / %0.1f MiB (%i%%)", titleIdHigh, titleIdLow, mcpInstallInfo[6] / (1024.0f * 1024.0f),
                                                                                                  mcpInstallInfo[4] / (1024.0f * 1024.0f), percent);
                        OSScreenPutFontEx(i, 0, 3, text);

                        if(percent == 100)
                        {
                            OSScreenPutFontEx(i, 0, 4, "Please wait...");
                        }
                        // Flip buffers
                        OSScreenFlipBuffersEx(i);
                    }
                }

                usleep(50000);
            }

            if(installError != 0)
            {
                if((installError == 0xFFFCFFE9) && installToUsb) {
                    __os_snprintf(text, sizeof(text), "Error: 0x%08X access failed (no USB storage attached?)", installError);
                }
                else {
                    __os_snprintf(text, sizeof(text), "Error: install error code 0x%08X", installError);
                }
                PrintError(text);
                break;
            }
            else
            {
                for(int i = 0; i < 2; i++)
                {
                    OSScreenClearBufferEx(i, 0);

                    OSScreenPutFontEx(i, 0, 0, TITLE_TEXT);
                    __os_snprintf(text, sizeof(text), "Installed title %08X-%08X successfully.", mcpInstallInfo[1], mcpInstallInfo[2]);
                    OSScreenPutFontEx(i, 0, 2, text);
                    // Flip buffers
                    OSScreenFlipBuffersEx(i);
                }
                installSuccess = 1;
            }
        }
        else
        {
            __os_snprintf(text, sizeof(text), "Error: Not a version title or a game update");
            PrintError(text);
        }

    }
    while(0);

    MCP_Close(mcpHandle);

    if(mcpPathInfoVector)
        OSFreeToSystem(mcpPathInfoVector);
    if(mcpInstallPath)
        OSFreeToSystem(mcpInstallPath);
    if(mcpInstallInfo)
        OSFreeToSystem(mcpInstallInfo);
}

static void CheckAndPrintInstallResult(void)
{
    OSScreenClearBufferEx(0, 0);
    OSScreenClearBufferEx(1, 0);

    if(!installSuccess)
    {
        // print to TV and DRC
        for(int i = 0; i < 2; i++)
        {
            OSScreenPutFontEx(i, 0, 0, TITLE_TEXT);
            OSScreenPutFontEx(i, 0, 2, "Press A-Button to install title to system memory.");
            OSScreenPutFontEx(i, 0, 3, "Press X-Button to install title to USB storage.");
            OSScreenPutFontEx(i, 0, 4, "Press HOME-Button to return to HBL.");
        }
    }
    else
    {
        // print to TV and DRC
        for(int i = 0; i < 2; i++)
        {
            OSScreenPutFontEx(i, 0, 0, TITLE_TEXT);

            char text[80];
            __os_snprintf(text, sizeof(text), "Install of title %08X-%08X finished successfully.", (u32)(installedTitle >> 32), (u32)(installedTitle & 0xffffffff));

            OSScreenPutFontEx(i, 0, 2, text);
            OSScreenPutFontEx(i, 0, 3, "You can eject the SD card now (if wanted).");

            OSScreenPutFontEx(i, 0, 5, "Press A-Button to install another title to system memory.");
            OSScreenPutFontEx(i, 0, 6, "Press X-Button to install another title to USB storage.");
            OSScreenPutFontEx(i, 0, 7, "Press HOME-Button to return to HBL.");
        }
    }

    // Flip buffers
    OSScreenFlipBuffersEx(0);
    OSScreenFlipBuffersEx(1);
}

/* Entry point */
int Menu_Main(void)
{
    //!*******************************************************************
    //!                   Initialize function pointers                   *
    //!*******************************************************************
    //! do OS (for acquire) and sockets first so we got logging
    InitOSFunctionPointers();
    InitSysFunctionPointers();
    InitVPadFunctionPointers();
    //InitSocketFunctionPointers();

    //log_init("192.168.178.3");

    //!*******************************************************************
    //!                    Initialize heap memory                        *
    //!*******************************************************************
    //! We don't need bucket and MEM1 memory so no need to initialize
    memoryInitialize();

    VPADInit();

    // Prepare screen
    int screen_buf0_size = 0;
    int screen_buf1_size = 0;

    // Init screen and screen buffers
    OSScreenInit();
    screen_buf0_size = OSScreenGetBufferSizeEx(0);
    screen_buf1_size = OSScreenGetBufferSizeEx(1);

    unsigned char *screenBuffer = MEM1_alloc(screen_buf0_size + screen_buf1_size, 0x40);

    OSScreenSetBufferEx(0, screenBuffer);
    OSScreenSetBufferEx(1, (screenBuffer + screen_buf0_size));

    OSScreenEnableEx(0, 1);
    OSScreenEnableEx(1, 1);

    // Clear screens
    OSScreenClearBufferEx(0, 0);
    OSScreenClearBufferEx(1, 0);

    u64 currenTitleId = OSGetTitleID();
    int hblChannelLaunch = (currenTitleId == 0x0005000013374842);

    // in case we are not in mii maker but in system menu we start the installation
    if (currenTitleId != 0x000500101004A200 && // mii maker eur
        currenTitleId != 0x000500101004A100 && // mii maker usa
        currenTitleId != 0x000500101004A000 && // mii maker jpn
        !hblChannelLaunch)                     // HBL channel
    {
        InstallTitle("/vol/app_sd/install", 0);

        MEM1_free(screenBuffer);
        memoryRelease();
        SYSLaunchTitle(baseTitleId);

        return EXIT_RELAUNCH_ON_LOAD;
    }

    // print result
    CheckAndPrintInstallResult();

    int doInstall = 0;
    int vpadError = -1;
    VPADData vpad;

    while(1)
    {
        VPADRead(0, &vpad, 1, &vpadError);

        if(vpadError == 0 && ((vpad.btns_d | vpad.btns_h) & VPAD_BUTTON_A))
        {
            baseTitleId = currenTitleId;
            installToUsb = 0;
            installSuccess = 0;
            installedTitle = 0;
            installCompleted = 0;
            installError = 0;

            //! HBL channel has all the rights to install a title. No need to launch menu.
            //! since it is always on redNAND or other CFW the white list can be ignored
            if(hblChannelLaunch)
            {
                InstallTitle("/vol/app_sd/install", 1);
                CheckAndPrintInstallResult();
            }
            else
            {
                doInstall = 1;
                break;
            }
        }

        if(vpadError == 0 && ((vpad.btns_d | vpad.btns_h) & VPAD_BUTTON_X))
        {
            baseTitleId = currenTitleId;
            installToUsb = 1;
            installSuccess = 0;
            installedTitle = 0;
            installCompleted = 0;
            installError = 0;

            //! HBL channel has all the rights to install a title. No need to launch menu.
            //! since it is always on redNAND or other CFW the white list can be ignored
            if(hblChannelLaunch)
            {
                InstallTitle("/vol/app_sd/install", 1);
                CheckAndPrintInstallResult();
            }
            else
            {
                doInstall = 1;
                break;
            }
        }

        if(vpadError == 0 && ((vpad.btns_d | vpad.btns_h) & VPAD_BUTTON_HOME))
            break;

		usleep(50000);
    }

	MEM1_free(screenBuffer);
	screenBuffer = NULL;

    //!*******************************************************************
    //!                    Enter main application                        *
    //!*******************************************************************
    memoryRelease();

    if(doInstall)
    {
        SYSLaunchMenu();
        return EXIT_RELAUNCH_ON_LOAD;
    }

    return EXIT_SUCCESS;
}

