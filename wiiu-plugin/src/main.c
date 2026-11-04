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
#include <sys/stat.h>
#include <stdbool.h>
#include <string.h>

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
        if (tid == 0x0005003010016000ULL || tid == 0x0005003010016100ULL || tid == 0x0005003010016200ULL)
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
