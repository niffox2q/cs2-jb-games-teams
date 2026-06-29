#include "jb_games_teams.h"
#include <random>
#include <cstdio>

#define MAX_PLAYERS 64

#define CS_TEAM_NONE 0
#define CS_TEAM_SPECTATOR 1
#define CS_TEAM_T 2
#define CS_TEAM_CT 3

jb_games_teams g_jb_games_teams;
PLUGIN_EXPOSE(jb_games_teams, g_jb_games_teams);


std::mt19937 g_Random;

// SYSTEM API`s
IVEngineServer2* engine = nullptr;
CGlobalVars* gpGlobals = nullptr;
CGameEntitySystem* g_pGameEntitySystem = nullptr;
CEntitySystem* g_pEntitySystem = nullptr;

// API
IUtilsApi* utils;
IMenusApi* menus_api;
IPlayersApi* players_api;
IJailbreakApi* jailbreak_api;

// VARS
bool bGameStarted = false;
std::map<std::string, std::string> phrases;

Color clrTeam1(255,0,0,255);
Color clrTeam2(0,0,255,255);
Color clrTeam3(0,255,0,255);
Color clrTeam4(255,255,0,255);

Color clrDefault(255,255,255,255);

Color g_TeamColors[5] = {clrDefault,clrTeam1,clrTeam2,clrTeam3,clrTeam4};

// =========================================
// CONFIG VARS
// =========================================
bool b_debug = true;

//==========================================
// HELPERS
//==========================================

void dbgmsg(const char* format, ...) {
    if (!b_debug) return;
    char buf[1024];
    va_list va;
    va_start(va, format);
    V_vsnprintf(buf, sizeof(buf), format, va);
    va_end(va);
    utils->PrintToChatAll("%s debug | %s", g_PLAPI->GetLogTag(), buf);
    META_CONPRINTF("%s debug | %s\n", g_PLAPI->GetLogTag(), buf);
}



// =========================================
// CONFIGS 
// =========================================

void LoadTranslations() {
    phrases.clear();
    KeyValues* g_kvPhrases = new KeyValues("Phrases");
    const char *pszPath = "addons/translations/jailbreak.phrases.txt";

    if (!g_kvPhrases->LoadFromFile(g_pFullFileSystem, pszPath))
    {
        utils->ErrorLog("%s Failed to load %s", g_PLAPI->GetLogTag(), pszPath);
        delete g_kvPhrases;
        return;
    }

    const char* language = utils->GetLanguage();

    for (KeyValues *pKey = g_kvPhrases->GetFirstTrueSubKey(); pKey; pKey = pKey->GetNextTrueSubKey()) {
        phrases[std::string(pKey->GetName())] = std::string(pKey->GetString(language));
    }
    delete g_kvPhrases;
}

const char* GetTranslation(const char* key) {
    auto it = phrases.find(key);
    if (it == phrases.end()) return key;
    else return it->second.c_str();
}

void PrintSlotPrefixed(int iSlot, const char* content) {
    if (!content || content[0] == '\0') return;
    char buf[512];
    g_SMAPI->Format(buf, sizeof(buf), "%s %s", GetTranslation("Prefix"), content);
    utils->PrintToChat(iSlot, buf);
}

void PrintAllPrefixed(const char* content) {
    if (!content || content[0] == '\0') return;
    char buf[512];
    g_SMAPI->Format(buf, sizeof(buf), "%s %s", GetTranslation("Prefix"), content);
    utils->PrintToChatAll(buf);
}



// =========================================
// OTHER
// =========================================


void ClearColor(){
    for (int i = 0; i < MAX_PLAYERS; i++) {
        auto pController = CCSPlayerController::FromSlot(i);
        if (!pController || !pController->IsConnected() || pController->GetTeam() != CS_TEAM_T) continue;
        if (jailbreak_api->IsPrisonerFreeday(i) || jailbreak_api->IsPrisonerRebel(i)) continue;

        auto pPawn = pController->GetPlayerPawn();
        if (!pPawn) continue;
        pPawn->m_clrRender = g_TeamColors[0];
        utils->SetStateChanged(pPawn,"CBaseModelEntity","m_clrRender");
    }
}

std::vector<int> GetAliveTerrorists() {
    std::vector<int> vAliveT;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        auto pController = CCSPlayerController::FromSlot(i);
        if (!pController || !pController->IsConnected() || pController->GetTeam() != CS_TEAM_T) continue;
        auto pPawn = pController->GetPlayerPawn();
        if (!pPawn || !pPawn->IsAlive()) continue;
        if (jailbreak_api->IsPrisonerFreeday(i)) continue;
        if (jailbreak_api->IsPrisonerRebel(i)) continue;

        vAliveT.push_back(i);
    }
    return vAliveT;
}

void TryMakeTeams(int iWarden, int iTeamsRequired) {
    std::vector<int> vAliveT = GetAliveTerrorists();

    if (vAliveT.empty() || vAliveT.size() < iTeamsRequired) {
        PrintSlotPrefixed(iWarden, GetTranslation("Teams_NotEnoughPlayers"));
        return;
    }

    std::shuffle(vAliveT.begin(), vAliveT.end(), g_Random);

    for (size_t i = 0; i < vAliveT.size(); ++i) {
        int iSlot = vAliveT[i];
        
        int iTeamIndex = (i % iTeamsRequired) + 1;

        auto pController = CCSPlayerController::FromSlot(iSlot);
        if (pController) {
            auto pPawn = pController->GetPlayerPawn();
            if (pPawn) {
                
                pPawn->m_clrRender = g_TeamColors[iTeamIndex];
                utils->SetStateChanged(pPawn, "CBaseModelEntity", "m_clrRender");
                switch (iTeamIndex) {
                    case 1: 
                        PrintSlotPrefixed(iSlot,GetTranslation("Teams_InTeamRed"));
                        break;
                    case 2: 
                        PrintSlotPrefixed(iSlot,GetTranslation("Teams_InTeamBlue"));
                        break;
                    case 3: 
                        PrintSlotPrefixed(iSlot,GetTranslation("Teams_InTeamGreen"));
                        break;
                    case 4: 
                        PrintSlotPrefixed(iSlot,GetTranslation("Teams_InTeamYellow"));
                        break;
                    default: 
                        break;
                }
            }
        }
    }

    bGameStarted = true;
    char msg[256];
    g_SMAPI->Format(msg, sizeof(msg), GetTranslation("Teams_GameStarted"), iTeamsRequired);
    PrintAllPrefixed(msg);
}



void WardenGameMenu(int iWarden){
    Menu hMenu;
    menus_api->SetTitleMenu(hMenu,GetTranslation("Teams_MenuTitle"));

    menus_api->AddItemMenu(hMenu,"clear",GetTranslation("Teams_ClearTeams"),ITEM_DEFAULT);
    menus_api->AddItemMenu(hMenu,"2",GetTranslation("Teams_TwoTeamsButton"),ITEM_DEFAULT);
    menus_api->AddItemMenu(hMenu,"3",GetTranslation("Teams_ThreeTeamsButton"),ITEM_DEFAULT);
    menus_api->AddItemMenu(hMenu,"4",GetTranslation("Teams_FourTeamsButton"),ITEM_DEFAULT);
    

    menus_api->SetCallback(hMenu,[](const char* szBack, const char* szFront, int iItem, int iSlot){
        if (!szBack || strcmp(szBack,"") == 0) return;
        if (strcmp(szBack,"exit") == 0) return;
        if (strcmp(szBack,"back") == 0) return;
        if (strcmp(szBack,"clear") == 0) {
            ClearColor();
            PrintAllPrefixed(GetTranslation("Teams_WardenClearTeams"));
            return;
        }

        int iTeamsChoosen = 0;
        iTeamsChoosen = atoi(szBack);
        if (iTeamsChoosen == 0) {
            PrintSlotPrefixed(iSlot,GetTranslation("Teams_ProblemDetected"));
            return;
        }
        TryMakeTeams(iSlot, iTeamsChoosen);
    });
    menus_api->SetExitMenu(hMenu,true);
    menus_api->DisplayPlayerMenu(hMenu,iWarden,true,true);
}

CGameEntitySystem* GameEntitySystem() {
    return utils ? utils->GetCGameEntitySystem() : nullptr;
}

void StartupServer() {
    g_pGameEntitySystem = GameEntitySystem();
    g_pEntitySystem = utils->GetCEntitySystem();
    gpGlobals = utils->GetCGlobalVars();
    std::random_device rd;
    g_Random.seed(rd());
}

bool jb_games_teams::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late) {
    PLUGIN_SAVEVARS();

    GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
    GET_V_IFACE_ANY(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
    GET_V_IFACE_ANY(GetServerFactory, g_pSource2Server, ISource2Server, SOURCE2SERVER_INTERFACE_VERSION);
    GET_V_IFACE_ANY(GetServerFactory, g_pSource2GameClients, IServerGameClients, SOURCE2GAMECLIENTS_INTERFACE_VERSION);
    GET_V_IFACE_ANY(GetServerFactory, g_pSource2GameEntities, ISource2GameEntities, SOURCE2GAMEENTITIES_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pNetworkSystem, INetworkSystem, NETWORKSYSTEM_INTERFACE_VERSION);

    ConVar_Register(FCVAR_SERVER_CAN_EXECUTE | FCVAR_GAMEDLL);
    g_SMAPI->AddListener(this, this);

    return true;
}



void jb_games_teams::AllPluginsLoaded() {
    int ret;
    utils = (IUtilsApi*)g_SMAPI->MetaFactory(Utils_INTERFACE, &ret, nullptr);
    if (ret == META_IFACE_FAILED) {
        META_CONPRINTF("%s | Missing UTILS plugin.",g_PLAPI->GetLogTag());
        engine->ServerCommand(("meta unload " + std::to_string(g_PLID)).c_str());
        return;
    }


    menus_api = (IMenusApi*)g_SMAPI->MetaFactory(Menus_INTERFACE, &ret, nullptr);
    if (ret == META_IFACE_FAILED) {
        META_CONPRINTF("%s | Missing UTILS plugin.",g_PLAPI->GetLogTag());
        engine->ServerCommand(("meta unload " + std::to_string(g_PLID)).c_str());
        return;
    }

    players_api = (IPlayersApi*)g_SMAPI->MetaFactory(PLAYERS_INTERFACE, &ret, nullptr);
    if (ret == META_IFACE_FAILED) {
        META_CONPRINTF("%s | Missing UTILS plugin.",g_PLAPI->GetLogTag());
        engine->ServerCommand(("meta unload " + std::to_string(g_PLID)).c_str());
        return;
    }

    jailbreak_api =(IJailbreakApi*)g_SMAPI->MetaFactory(JAILBREAK_INTERFACE, &ret, nullptr);
    if (ret == META_IFACE_FAILED) {
        META_CONPRINTF("%s | Missing Jailbreak Core plugin.",g_PLAPI->GetLogTag());
        engine->ServerCommand(("meta unload " + std::to_string(g_PLID)).c_str());
        return;
    }

    LoadTranslations();

    jailbreak_api->RegisterGameFeature(g_PLID,"teams",GetTranslation("Teams_WardenGameButton"),[](int iWarden){
        WardenGameMenu(iWarden);
    });

    jailbreak_api->OnWardenDieListener(g_PLID,[](int iSlot){
        ClearColor();
        if (bGameStarted) {
            PrintAllPrefixed(GetTranslation("Teams_WardenDieGameEnd"));
        }
        bGameStarted = false;
    });

    utils->HookEvent(g_PLID,"round_end",[](const char* szName, IGameEvent* pEvent, bool bDontBroadcast){
        ClearColor();
        bGameStarted = false;
    });


    utils->StartupServer(g_PLID, StartupServer);

    
    
}

bool jb_games_teams::Unload(char* error, size_t maxlen) {
    jailbreak_api->ClearAllPluginHooks(g_PLID);
    utils->ClearAllHooks(g_PLID);
    ConVar_Unregister();

   
    return true;
}

const char* jb_games_teams::GetAuthor() { return "niffox"; }
const char* jb_games_teams::GetDate() { return __DATE__; }
const char* jb_games_teams::GetDescription() { return "[JB] Team Games"; }
const char* jb_games_teams::GetLicense() { return "Private"; }
const char* jb_games_teams::GetLogTag() { return "[JB] Team Games"; }
const char* jb_games_teams::GetName() { return "[JB] Team Games"; }
const char* jb_games_teams::GetURL() { return "https://t.me/niffox_2q"; }
const char* jb_games_teams::GetVersion() { return "1.1"; }