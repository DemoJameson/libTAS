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

#include "steamapiinternal.h"
#include "steamapi.h"
#include "isteamremotestorage/isteamremotestorage.h"
#include "isteamapps.h"
#include "isteamugc.h"
#include "isteamuserstats.h"

#include "logging.h"
#include "hook.h"
#include "global.h"
#include "GlobalState.h"

#include <dlfcn.h>

namespace libtas {

DEFINE_ORIG_POINTER(SteamAPI_GetHSteamUser)
DEFINE_ORIG_POINTER(SteamAPI_GetHSteamPipe)
DEFINE_ORIG_POINTER(SteamInternal_ContextInit)
DEFINE_ORIG_POINTER(SteamInternal_CreateInterface)
DEFINE_ORIG_POINTER(SteamInternal_FindOrCreateUserInterface)
DEFINE_ORIG_POINTER(SteamInternal_FindOrCreateGameServerInterface)
DEFINE_ORIG_POINTER(SteamInternal_SteamAPI_Init)
DEFINE_ORIG_POINTER(_ZN16CSteamAPIContext4InitEv)

HSteamUser SteamAPI_GetHSteamUser()
{
    LOGTRACE(LCF_STEAM);
    if (!Global::shared_config.virtual_steam) {
        LINK_NAMESPACE(SteamAPI_GetHSteamUser, "steam_api");
        return orig::SteamAPI_GetHSteamUser();
    }

    return 1;
}

HSteamPipe SteamAPI_GetHSteamPipe()
{
    LOGTRACE(LCF_STEAM);
    if (!Global::shared_config.virtual_steam) {
        LINK_NAMESPACE(SteamAPI_GetHSteamUser, "steam_api");
        return orig::SteamAPI_GetHSteamUser();
    }
    
    return true;
}

CSteamAPIContext* SteamInternal_ContextInit( CSteamAPIContextInitData *data )
{
    LOGTRACE(LCF_STEAM);
    if (!Global::shared_config.virtual_steam) {
        LINK_NAMESPACE(SteamInternal_ContextInit, "steam_api");
        return orig::SteamInternal_ContextInit(data);
    }

    /* Should be incremented on API/GameServer Init/Shutdown. */
    const uintptr_t ifaces_stale_cnt = 1;
    if (data->ifaces_stale_cnt != ifaces_stale_cnt)
    {
        if (data->callback)
            data->callback(&data->ctx);
    
        data->ifaces_stale_cnt = ifaces_stale_cnt;
    }
    
    return &data->ctx;
}

void * SteamInternal_CreateInterface( const char *ver )
{
    LOG(LL_TRACE, LCF_STEAM, "%s called with %s", __func__, ver);
    if (!Global::shared_config.virtual_steam) {
        LINK_NAMESPACE(SteamInternal_CreateInterface, "steam_api");
        return orig::SteamInternal_CreateInterface(ver);
    }

    /* The expected return from this function is a pointer to a C++ class with
     * specific virtual functions.  The format of our argument is the name
     * of the corresponding C function that has already been hooked to return
     * the correct value, followed by some numbers that are probably used for
     * version checking.  As a quick hack, just lookup the symbol and call it.
     */
    std::string symbol = ver;
    /* Strip numbers at the end */
    auto end = symbol.find_last_not_of("0123456789");
    if (end != std::string::npos)
        symbol.resize(end + 1);
        
    /* For some interfaces, the version string is named differently, I'm
     * checking specifically for those for now */
    if (0 == symbol.compare("STEAMAPPS_INTERFACE_VERSION"))
        return SteamApps();
    else if (0 == symbol.compare("STEAMHTMLSURFACE_INTERFACE_VERSION_")) // Not a typo
        symbol = "SteamHTMLSurface";
    else if (0 == symbol.compare("STEAMMUSIC_INTERFACE_VERSION"))
        symbol = "SteamMusic";
    else if (0 == symbol.compare("STEAMMUSICREMOTE_INTERFACE_VERSION"))
        symbol = "SteamMusicRemote";
    else if (0 == symbol.compare("STEAMREMOTESTORAGE_INTERFACE_VERSION")) {
        SteamRemoteStorage_set_version(ver);
        return SteamRemoteStorage();
    }
    else if (0 == symbol.compare("STEAMSCREENSHOTS_INTERFACE_VERSION"))
        symbol = "SteamScreenshots";
    else if (0 == symbol.compare("STEAMUGC_INTERFACE_VERSION"))
        return SteamUGC();
    else if (0 == symbol.compare("STEAMUSERSTATS_INTERFACE_VERSION"))
        return SteamUserStats();
    else if (0 == symbol.compare("STEAMVIDEO_INTERFACE_V")) // Not a typo
        symbol = "SteamVideo";    
    
    void *(*func)() = reinterpret_cast<void *(*)()>(dlsym(RTLD_DEFAULT, symbol.c_str()));
    if (func)
        return func();
    return nullptr;
}

void * SteamInternal_FindOrCreateUserInterface(HSteamUser steam_user, const char *version)
{
    LOG(LL_TRACE, LCF_STEAM, "%s called with version %s", __func__, version);
    if (!Global::shared_config.virtual_steam) {
        LINK_NAMESPACE(SteamInternal_FindOrCreateUserInterface, "steam_api");
        return orig::SteamInternal_FindOrCreateUserInterface(steam_user, version);
    }

    /* The expected return from this function is a pointer to a C++ class with
     * specific virtual functions.  The format of our argument is the name
     * of the corresponding C function that has already been hooked to return
     * the correct value, followed by some numbers that are probably used for
     * version checking.  As a quick hack, just lookup the symbol and call it.
     */
    std::string symbol = version;
    /* Strip numbers at the end */
    auto end = symbol.find_last_not_of("0123456789");
    if (end != std::string::npos)
        symbol.resize(end + 1);
        
    /* For some interfaces, the version string is named differently, I'm
     * checking specifically for those for now */
    if (0 == symbol.compare("STEAMAPPS_INTERFACE_VERSION"))
        return SteamApps();
    else if (0 == symbol.compare("STEAMHTMLSURFACE_INTERFACE_VERSION_")) // Not a typo
        symbol = "SteamHTMLSurface";
    else if (0 == symbol.compare("STEAMMUSIC_INTERFACE_VERSION"))
        symbol = "SteamMusic";
    else if (0 == symbol.compare("STEAMMUSICREMOTE_INTERFACE_VERSION"))
        symbol = "SteamMusicRemote";
    else if (0 == symbol.compare("STEAMREMOTESTORAGE_INTERFACE_VERSION")) {
        SteamRemoteStorage_set_version(version);
        return SteamRemoteStorage();
    }
    else if (0 == symbol.compare("STEAMSCREENSHOTS_INTERFACE_VERSION"))
        symbol = "SteamScreenshots";
    else if (0 == symbol.compare("STEAMUGC_INTERFACE_VERSION"))
        return SteamUGC();
    else if (0 == symbol.compare("STEAMUSERSTATS_INTERFACE_VERSION"))
        return SteamUserStats();
    else if (0 == symbol.compare("STEAMVIDEO_INTERFACE_V")) // Not a typo
        symbol = "SteamVideo";    
    
    void *(*func)() = reinterpret_cast<void *(*)()>(dlsym(RTLD_DEFAULT, symbol.c_str()));
    if (func)
        return func();
    return nullptr;
}

void * SteamInternal_FindOrCreateGameServerInterface(HSteamUser steam_user, const char *version)
{
    LOG(LL_TRACE, LCF_STEAM, "%s called with version %s", __func__, version);
    if (!Global::shared_config.virtual_steam) {
        LINK_NAMESPACE(SteamInternal_FindOrCreateGameServerInterface, "steam_api");
        return orig::SteamInternal_FindOrCreateGameServerInterface(steam_user, version);
    }

    /* The expected return from this function is a pointer to a C++ class with
     * specific virtual functions.  The format of our argument is the name
     * of the corresponding C function that has already been hooked to return
     * the correct value, followed by some numbers that are probably used for
     * version checking.  As a quick hack, just lookup the symbol and call it.
     */
    std::string symbol = version;
    /* Strip numbers at the end */
    auto end = symbol.find_last_not_of("0123456789");
    if (end != std::string::npos)
        symbol.resize(end + 1);
        
    /* For some interfaces, the version string is named differently, I'm
     * checking specifically for those for now */
    if (0 == symbol.compare("STEAMAPPS_INTERFACE_VERSION"))
        return SteamApps();
    else if (0 == symbol.compare("STEAMHTMLSURFACE_INTERFACE_VERSION_")) // Not a typo
        symbol = "SteamHTMLSurface";
    else if (0 == symbol.compare("STEAMMUSIC_INTERFACE_VERSION"))
        symbol = "SteamMusic";
    else if (0 == symbol.compare("STEAMMUSICREMOTE_INTERFACE_VERSION"))
        symbol = "SteamMusicRemote";
    else if (0 == symbol.compare("STEAMREMOTESTORAGE_INTERFACE_VERSION")) {
        SteamRemoteStorage_set_version(version);
        return SteamRemoteStorage();
    }
    else if (0 == symbol.compare("STEAMSCREENSHOTS_INTERFACE_VERSION"))
        symbol = "SteamScreenshots";
    else if (0 == symbol.compare("STEAMUGC_INTERFACE_VERSION"))
        return SteamUGC();
    else if (0 == symbol.compare("STEAMUSERSTATS_INTERFACE_VERSION"))
        return SteamUserStats();
    else if (0 == symbol.compare("STEAMVIDEO_INTERFACE_V")) // Not a typo
        symbol = "SteamVideo";    
    
    void *(*func)() = reinterpret_cast<void *(*)()>(dlsym(RTLD_DEFAULT, symbol.c_str()));
    if (func)
        return func();
    return nullptr;
}

bool _ZN16CSteamAPIContext4InitEv(CSteamAPIContext* context)
{
    LOGTRACE(LCF_STEAM);
    if (!Global::shared_config.virtual_steam) {
        LINK_NAMESPACE(_ZN16CSteamAPIContext4InitEv, "steam_api");
        return orig::_ZN16CSteamAPIContext4InitEv(context);
    }

    GlobalNoLog gnl;
    context->m_pSteamClient = SteamClient();
    context->m_pSteamUser = SteamUser();
    context->m_pSteamUserStats = SteamUserStats();
    context->m_pSteamUtils = SteamUtils();
    context->m_pSteamRemoteStorage = SteamRemoteStorage();
    context->m_pSteamApps = SteamApps();
    context->m_pSteamFriends = SteamFriends();
    context->m_pSteamScreenshots = SteamScreenshots();
    context->m_pSteamUGC = SteamUGC();
    context->m_pSteamMatchmaking = SteamMatchmaking();
    context->m_pSteamMatchmakingServers = SteamMatchmakingServers();
    context->m_pSteamHTTP = SteamHTTP();
    context->m_pSteamNetworking = SteamNetworking();
    context->m_pController = SteamController();
    context->m_pSteamAppList = nullptr;
    context->m_pSteamMusic = nullptr;
    context->m_pSteamMusicRemote = nullptr;
    context->m_pSteamHTMLSurface = nullptr;
    context->m_pSteamInventory = nullptr;
    context->m_pSteamVideo = nullptr;
    context->m_pSteamParentalSettings = nullptr;

    return true;
}

int SteamInternal_SteamAPI_Init( const char *pszVersion, char** error )
{
    LOGTRACE(LCF_STEAM);
    if (Global::shared_config.virtual_steam) {
        return 0; // k_ESteamAPIInitResult_OK
    }
    LINK_NAMESPACE(SteamInternal_SteamAPI_Init, "steam_api");
    return orig::SteamInternal_SteamAPI_Init(pszVersion, error);
}

}
