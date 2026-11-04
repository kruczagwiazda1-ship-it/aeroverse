/*
 * Aeroverse Button - Aroma plugin
 * Wybrany przycisk w Wii U Menu (domyslnie Menedzer pobierania) otwiera Aeroverse.
 * Miiverse / Pretendo (Juxt) zostaje nietkniete, chyba ze wybierzesz tryb Miiverse.
 */
#include <wups.h>
#include <wups/config/WUPSConfigItemBoolean.h>
#include <wups/config/WUPSConfigItemMultipleValues.h>
#include <rpxloader/rpxloader.h>
#include <sysapp/switch.h>
#include <coreinit/title.h>
#include <coreinit/filesystem.h>
#include <nn/acp/title.h>
#include <ctype.h>
#include "icon_tga.h"
#include <sys/stat.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

WUPS_PLUGIN_NAME("Aeroverse Button");
WUPS_PLUGIN_DESCRIPTION("Wybrany przycisk Wii U Menu otwiera Aeroverse");
WUPS_PLUGIN_VERSION("v1.0");
WUPS_PLUGIN_AUTHOR("Aeroverse");
WUPS_PLUGIN_LICENSE("MIT");

WUPS_USE_WUT_DEVOPTAB();
WUPS_USE_STORAGE("aeroverse_button");

#define CFG_MODE     "button"

enum { MODE_OFF = 0, MODE_TVII = 1, MODE_MIIVERSE = 2, MODE_DLMGR = 3, MODE_BROWSER = 4 };
#define CFG_FALLBACK "fallback"

/* Aplikacja na SD (tak samo jak w aktualizatorze Aeroverse) */
#define SD_PREFIX  "fs:/vol/external01/"
#define APP_REL_1  "wiiu/apps/aeroverse.wuhb"
#define APP_REL_2  "wiiu/apps/aeroverse_update.wuhb"

static int32_t gMode  = MODE_DLMGR;
static bool gFallback = false;
static bool gRpxOk    = false;

/* Wybiera nowszy z dwoch plikow .wuhb; NULL gdy brak obu */
static const char *pickApp(void) {
    struct stat a, b;
    bool hasA = stat(SD_PREFIX APP_REL_1, &a) == 0;
    bool hasB = stat(SD_PREFIX APP_REL_2, &b) == 0;
    if (hasA && hasB) return (b.st_mtime > a.st_mtime) ? APP_REL_2 : APP_REL_1;
    if (hasA) return APP_REL_1;
    if (hasB) return APP_REL_2;
    return NULL;
}

/* true = Aeroverse sie uruchamia */
static bool launchAeroverse(void) {
    if (gMode == MODE_OFF || !gRpxOk) return false;
    const char *app = pickApp();
    if (!app) return false;
    return RPXLoader_LaunchHomebrew(app) == RPX_LOADER_RESULT_SUCCESS;
}

/* czy ten przycisk ma otwierac Aeroverse */
static bool isTarget(SysAppPFID pfid) {
    if (gMode == MODE_TVII) return pfid == SYSAPP_PFID_TVII;
    if (gMode == MODE_MIIVERSE) return pfid == SYSAPP_PFID_MIIVERSE || pfid == SYSAPP_PFID_MINI_MIIVERSE;
    if (gMode == MODE_DLMGR) return pfid == SYSAPP_PFID_DOWNLOAD_MANAGEMENT;
    if (gMode == MODE_BROWSER) return pfid == SYSAPP_PFID_BROWSER;
    return false;
}

/* ---- konfiguracja (L + w dol + SELECT w Aroma) ---- */
static void modeChanged(ConfigItemMultipleValues *item, uint32_t v) {
    gMode = (int32_t) v;
    WUPSStorageAPI_StoreInt(NULL, CFG_MODE, gMode);
}
static void fallbackChanged(ConfigItemBoolean *item, bool v) {
    gFallback = v;
    WUPSStorageAPI_StoreBool(NULL, CFG_FALLBACK, v);
}

static WUPSConfigAPICallbackStatus ConfigOpened(WUPSConfigCategoryHandle root) {
    static ConfigItemMultipleValuesPair modes[] = {
        {MODE_OFF, "Wylaczony"},
        {MODE_TVII, "TVii (USA/JPN)"},
        {MODE_MIIVERSE, "Miiverse"},
        {MODE_DLMGR, "Menedzer pobierania"},
        {MODE_BROWSER, "Przegladarka"},
    };
    int cur = (gMode >= 0 && gMode <= 4) ? gMode : MODE_DLMGR;
    if (WUPSConfigItemMultipleValues_AddToCategory(root, CFG_MODE, "Przycisk Aeroverse", MODE_DLMGR, cur,
                                                   modes, 5, &modeChanged) != WUPSCONFIG_API_RESULT_SUCCESS)
        return WUPSCONFIG_API_CALLBACK_RESULT_ERROR;
    if (WUPSConfigItemBoolean_AddToCategory(root, CFG_FALLBACK, "Tryb awaryjny (tylko Miiverse)",
                                            false, gFallback, &fallbackChanged) != WUPSCONFIG_API_RESULT_SUCCESS)
        return WUPSCONFIG_API_CALLBACK_RESULT_ERROR;
    return WUPSCONFIG_API_CALLBACK_RESULT_SUCCESS;
}
static void ConfigClosed(void) { WUPSStorageAPI_SaveStorage(false); }

static void loadBool(const char *key, bool *val) {
    if (WUPSStorageAPI_GetBool(NULL, key, val) == WUPS_STORAGE_ERROR_NOT_FOUND)
        WUPSStorageAPI_StoreBool(NULL, key, *val);
}
static void loadInt(const char *key, int32_t *val) {
    if (WUPSStorageAPI_GetInt(NULL, key, val) == WUPS_STORAGE_ERROR_NOT_FOUND)
        WUPSStorageAPI_StoreInt(NULL, key, *val);
}

INITIALIZE_PLUGIN() {
    WUPSConfigAPIOptionsV1 opts = {.name = "Aeroverse Button"};
    WUPSConfigAPI_Init(opts, ConfigOpened, ConfigClosed);
    loadInt(CFG_MODE, &gMode);
    loadBool(CFG_FALLBACK, &gFallback);
    WUPSStorageAPI_SaveStorage(false);
}

ON_APPLICATION_START() {
    gRpxOk = RPXLoader_InitLibrary() == RPX_LOADER_RESULT_SUCCESS;

    /* Tryb awaryjny: gdy jednak wystartuje aplet Miiverse, przeskocz do Aeroverse */
    if (gFallback && gMode == MODE_MIIVERSE) {
        uint64_t tid = OSGetTitleID();
        if ((uint32_t)(tid >> 32) == 0x00050030 && (((uint32_t) tid) >> 12) == 0x10016) /* aplet Miiverse, kazdy region */
            launchAeroverse();
    }
}

ON_APPLICATION_ENDS() {
    if (gRpxOk) RPXLoader_DeInitLibrary();
    gRpxOk = false;
}

/* ---- podmiana przycisku w Wii U Menu ---- */
DECL_FUNCTION(int32_t, SYSSwitchTo, SysAppPFID pfid) {
    if (isTarget(pfid) && launchAeroverse()) return 0;
    return real_SYSSwitchTo(pfid);
}
DECL_FUNCTION(int32_t, _SYSSwitchTo, SysAppPFID pfid) {
    if (isTarget(pfid) && launchAeroverse()) return 0;
    return real__SYSSwitchTo(pfid);
}
DECL_FUNCTION(int32_t, _SYSDirectlySwitchTo, SysAppPFID pfid) {
    if (isTarget(pfid) && launchAeroverse()) return 0;
    return real__SYSDirectlySwitchTo(pfid);
}

WUPS_MUST_REPLACE_FOR_PROCESS(SYSSwitchTo, WUPS_LOADER_LIBRARY_SYSAPP, SYSSwitchTo, WUPS_FP_TARGET_PROCESS_WII_U_MENU);
WUPS_MUST_REPLACE_FOR_PROCESS(_SYSSwitchTo, WUPS_LOADER_LIBRARY_SYSAPP, _SYSSwitchTo, WUPS_FP_TARGET_PROCESS_WII_U_MENU);
WUPS_MUST_REPLACE_FOR_PROCESS(_SYSDirectlySwitchTo, WUPS_LOADER_LIBRARY_SYSAPP, _SYSDirectlySwitchTo, WUPS_FP_TARGET_PROCESS_WII_U_MENU);

/* ---- ikonka i nazwa Aeroverse na wybranym przycisku ----
 * Wii U Menu bierze ikonki gornych przyciskow z meta/iconTex.tga apletow (00050030/1001X?0A).
 * Podmieniamy ten plik na wbudowana ikonke Aeroverse i nazwe w meta.xml. */

/* 5 pierwszych cyfr dolnej czesci Title ID apletu dla wybranego przycisku, 0 = brak */
static uint32_t targetApplet(void) {
    switch (gMode) {
        case MODE_TVII: return 0x10013;
        case MODE_MIIVERSE: return 0x10016;
        case MODE_DLMGR: return 0x10018;
        case MODE_BROWSER: return 0x10012;
        default: return 0;
    }
}

static bool endsWithI(const char *s, const char *suf) {
    size_t a = strlen(s), b = strlen(suf);
    if (a < b) return false;
    for (size_t i = 0; i < b; i++)
        if (tolower((unsigned char) s[a - b + i]) != tolower((unsigned char) suf[i])) return false;
    return true;
}

/* czy sciezka to iconTex.tga apletu, ktory podmieniamy */
static bool isTargetIcon(const char *path) {
    uint32_t t = targetApplet();
    if (!t || !path || !endsWithI(path, "/iconTex.tga")) return false;
    const char *p = strstr(path, "00050030/");
    if (!p) return false;
    char want[6];
    snprintf(want, sizeof(want), "%05x", (unsigned) t);
    for (int i = 0; i < 5; i++)
        if (tolower((unsigned char) p[9 + i]) != want[i]) return false;
    return true;
}

typedef struct { bool used; uint32_t pos; } FakeFile;
static FakeFile gFake[4];

static FakeFile *fakeFromHandle(FSFileHandle h) {
    for (int i = 0; i < 4; i++)
        if (gFake[i].used && (FSFileHandle)(uintptr_t) &gFake[i] == h) return &gFake[i];
    return NULL;
}

DECL_FUNCTION(FSStatus, FSOpenFile, FSClient *client, FSCmdBlock *block, const char *path, const char *mode,
              FSFileHandle *handle, FSErrorFlag errorMask) {
    if (isTargetIcon(path) && handle) {
        for (int i = 0; i < 4; i++) {
            if (!gFake[i].used) {
                gFake[i].used = true;
                gFake[i].pos  = 0;
                *handle       = (FSFileHandle)(uintptr_t) &gFake[i];
                return FS_STATUS_OK;
            }
        }
    }
    return real_FSOpenFile(client, block, path, mode, handle, errorMask);
}

DECL_FUNCTION(FSStatus, FSReadFile, FSClient *client, FSCmdBlock *block, uint8_t *buffer, uint32_t size,
              uint32_t count, FSFileHandle handle, uint32_t unk1, FSErrorFlag errorMask) {
    FakeFile *f = fakeFromHandle(handle);
    if (f) {
        if (size == 0) return (FSStatus) 0;
        uint32_t left = icon_tga_size - f->pos;
        uint32_t want = size * count;
        if (want > left) want = left;
        want -= want % size;
        memcpy(buffer, icon_tga + f->pos, want);
        f->pos += want;
        return (FSStatus)(want / size);
    }
    return real_FSReadFile(client, block, buffer, size, count, handle, unk1, errorMask);
}

DECL_FUNCTION(FSStatus, FSCloseFile, FSClient *client, FSCmdBlock *block, FSFileHandle handle, FSErrorFlag errorMask) {
    FakeFile *f = fakeFromHandle(handle);
    if (f) {
        f->used = false;
        return FS_STATUS_OK;
    }
    return real_FSCloseFile(client, block, handle, errorMask);
}

static void setName(char *dst, size_t n) { strncpy(dst, "Aeroverse", n - 1); dst[n - 1] = 0; }

DECL_FUNCTION(int32_t, ACPGetTitleMetaXmlByDevice, uint32_t upper, uint32_t lower, ACPMetaXml *out, uint32_t device, uint32_t u1) {
    int32_t res = real_ACPGetTitleMetaXmlByDevice(upper, lower, out, device, u1);
    uint32_t t  = targetApplet();
    if (res == 0 && out && t && upper == 0x00050030 && (lower >> 12) == t) {
        char *ln = out->longname_ja, *sn = out->shortname_ja;
        for (int i = 0; i < 12; i++) {
            setName(ln + i * 512, 512);
            setName(sn + i * 256, 256);
        }
    }
    return res;
}

WUPS_MUST_REPLACE_FOR_PROCESS(FSOpenFile, WUPS_LOADER_LIBRARY_COREINIT, FSOpenFile, WUPS_FP_TARGET_PROCESS_WII_U_MENU);
WUPS_MUST_REPLACE_FOR_PROCESS(FSReadFile, WUPS_LOADER_LIBRARY_COREINIT, FSReadFile, WUPS_FP_TARGET_PROCESS_WII_U_MENU);
WUPS_MUST_REPLACE_FOR_PROCESS(FSCloseFile, WUPS_LOADER_LIBRARY_COREINIT, FSCloseFile, WUPS_FP_TARGET_PROCESS_WII_U_MENU);
WUPS_MUST_REPLACE_FOR_PROCESS(ACPGetTitleMetaXmlByDevice, WUPS_LOADER_LIBRARY_NN_ACP, ACPGetTitleMetaXmlByDevice, WUPS_FP_TARGET_PROCESS_WII_U_MENU);
