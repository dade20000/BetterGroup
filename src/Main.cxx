#include <TruckersMP/TruckersMP.hxx>

#include <windows.h>
#include <winhttp.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "shell32.lib")

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

static HMODULE g_module = nullptr;
static std::unique_ptr<TruckersMP::Session> g_session;
static bool g_debug=false;

template<class T>
static void Rel(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

struct RGB {
    uint8_t r=255, g=255, b=255;

    bool operator==(const RGB& o) const {
        return r==o.r && g==o.g && b==o.b;
    }
};

struct VisiblePlayer {
    int playerId=0;
    uint64_t accountId=0;
    std::string username;
    bool local=false;
};

struct MarkerRect {
    int x=0, y=0, w=0, h=0;
};

struct ChatStaffEvent {
    RGB color;
    int playerId=-1;
    Clock::time_point when{};
};

struct PanelStaffEntry {
    RGB color;
    std::string username;
    int playerId=-1;
    bool local=false;
};

static bool g_localGroupVisible=true;
static bool g_localGroupVisibilityKnown=false;
static Clock::time_point g_lastGroupConfigPoll{};
static bool g_cachedLocalShieldValid=false;
static MarkerRect g_cachedLocalShield{};
static RGB g_cachedLocalShieldColor{};
struct PlayerPanelGeometry {
    int x=0;
    int y=0;
    int w=0;
    int h=0;
    bool valid=false;
};

static PlayerPanelGeometry g_playerPanelGeometry{};

struct PanelDragFollowState {
    bool buttonWasDown=false;
    bool active=false;
    bool releaseHold=false;

    POINT startCursor{};
    POINT currentCursor{};

    int dx=0;
    int dy=0;

    std::vector<MarkerRect> baseMarkers;
    Clock::time_point releasedAt{};
};

static PanelDragFollowState g_panelDrag{};

// -----------------------------------------------------------------------------
// Paths / log
// -----------------------------------------------------------------------------

static std::string PluginDir() {
    wchar_t path[MAX_PATH]{};
    if (!g_module) return ".";

    GetModuleFileNameW(g_module, path, MAX_PATH);
    std::wstring ws(path);
    auto p = ws.find_last_of(L"\\/");
    if (p != std::wstring::npos) ws.resize(p);

    int n = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return ".";

    std::string out((size_t)n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, out.data(), n, nullptr, nullptr);
    return out;
}

static void Log(const std::string& s) {
    if(!g_debug) return;

    std::ofstream f(
        PluginDir() + "\\BetterGroup.log",
        std::ios::app);

    if(f) f << s << "\n";

    if(g_session) {
        g_session->Core().LogMessage(
            TruckersMP::LogLevel::Info,
            s);
    }
}

static std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static bool ParseHex(const std::string& s, RGB& c) {
    std::string h = s;
    if (!h.empty() && h[0] == '#') h.erase(h.begin());
    if (h.size() != 6) return false;

    try {
        c.r = (uint8_t)std::stoul(h.substr(0,2), nullptr, 16);
        c.g = (uint8_t)std::stoul(h.substr(2,2), nullptr, 16);
        c.b = (uint8_t)std::stoul(h.substr(4,2), nullptr, 16);
        return true;
    } catch (...) {
        return false;
    }
}

// -----------------------------------------------------------------------------
// Meet Staff role -> color
// -----------------------------------------------------------------------------

static std::unordered_map<std::string, RGB> g_roleColors;
static std::vector<std::pair<std::string, RGB>> g_rolesByPriority;

static void AddRole(const std::string& role, const RGB& c) {
    const std::string key = Lower(role);
    g_roleColors[key] = c;

    bool found=false;
    for (auto& p : g_rolesByPriority) {
        if (p.first == key) {
            p.second = c;
            found=true;
            break;
        }
    }
    if (!found) g_rolesByPriority.push_back({key,c});
}

static void LoadRoles() {
    struct RoleDefault { const char* role; const char* hex; };

    static const RoleDefault defaults[] = {
        {"Game Producer","7c4dff"},
        {"Game Developer","673ab7"},
        {"DevOps","673ab7"},
        {"Web Developer","673ab7"},
        {"Project Manager","9575cd"},
        {"Vice Project Manager","9575cd"},
        {"Business Analyst","5dc2b0"},
        {"Senior Community Manager","bf360c"},
        {"Senior Game Moderation Manager","b71c1c"},
        {"Senior Event Manager","0d47a1"},
        {"Community Manager","e64a19"},
        {"Community Moderation Manager","00838f"},
        {"Game Moderation Manager","d32f2f"},
        {"Support Manager","f21f6c"},
        {"Add-On Manager","7e57c2"},
        {"Event Manager","1565c0"},
        {"Media Manager","ff8600"},
        {"Translation Manager","00b8d4"},
        {"Game Moderation Leader","ff1744"},
        {"Game Moderation Trainer","ff1744"},
        {"Game Moderator","f44336"},
        {"Report Moderator","ff5252"},
        {"Game Moderation Trainee","ff8a80"},
        {"Support Team Leader","e3467a"},
        {"Support","f06292"},
        {"Trial Support","f48fb1"},
        {"Community Moderation Leader","0097a7"},
        {"Community Moderator","00acc1"},
        {"Community Moderation Trainee","00acc1"},
        {"Translation Team Leader","00cceb"},
        {"Translator","00e5ff"},
        {"Translation Trainee","84ffff"},
        {"Add-On Team","b388ff"},
        {"Media Team Leader","ff8f00"},
        {"Media Team","ff9800"},
        {"Official Streamer","00ffc4"},
        {"Event Planner","366fb5"},
        {"Event Team","1e88e5"},
        {"Test Coordinator","a2b2b9"},
        {"Testing Team","cc33c7"}
    };

    g_rolesByPriority.clear();

    for (const auto& e : defaults) {
        RGB c;
        if (ParseHex(e.hex, c)) AddRole(e.role, c);
    }

    std::ifstream f(PluginDir() + "\\BetterGroup_roles.cfg");
    if (!f) return;

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;

        auto p1 = line.find('|');
        auto p2 = (p1 == std::string::npos) ? std::string::npos : line.find('|', p1 + 1);
        if (p1 == std::string::npos || p2 == std::string::npos) continue;

        std::string role = line.substr(p1 + 1, p2 - p1 - 1);
        std::string hex = line.substr(p2 + 1);

        while (!role.empty() && std::isspace((unsigned char)role.front())) role.erase(role.begin());
        while (!role.empty() && std::isspace((unsigned char)role.back())) role.pop_back();
        while (!hex.empty() && std::isspace((unsigned char)hex.front())) hex.erase(hex.begin());
        while (!hex.empty() && std::isspace((unsigned char)hex.back())) hex.pop_back();

        RGB c;
        if (ParseHex(hex, c)) {
            // Preserve configured priority from the file:
            // role already exists in the same default order for this package.
            AddRole(role, c);
        }
    }
}

// -----------------------------------------------------------------------------
// Web API resolver
// -----------------------------------------------------------------------------

static std::unordered_map<uint64_t, RGB> g_staffColorByAccount;
static std::unordered_set<uint64_t> g_resolvedAccounts;
static std::unordered_map<uint64_t, Clock::time_point> g_retryAfter;
static std::unordered_set<uint64_t> g_pending;
static std::queue<uint64_t> g_lookupQueue;
static std::mutex g_apiMutex;
static std::condition_variable g_apiCv;
static std::thread g_apiThread;
static std::atomic<bool> g_stop{false};

static bool JsonString(const std::string& body, const std::string& key, std::string& out) {
    const std::string needle = "\"" + key + "\"";
    auto p = body.find(needle);
    if (p == std::string::npos) return false;

    p = body.find(':', p + needle.size());
    if (p == std::string::npos) return false;
    ++p;

    while (p < body.size() && std::isspace((unsigned char)body[p])) ++p;
    if (p >= body.size() || body[p] != '"') return false;
    ++p;

    std::string value;
    bool escaped = false;

    for (; p < body.size(); ++p) {
        const char ch = body[p];

        if (escaped) {
            value.push_back(ch);
            escaped = false;
            continue;
        }

        if (ch == '\\') {
            escaped = true;
            continue;
        }

        if (ch == '"') {
            out = value;
            return true;
        }

        value.push_back(ch);
    }

    return false;
}

static bool FetchGroupName(uint64_t accountId, std::string& groupName) {
    HINTERNET session = WinHttpOpen(
        L"BetterGroup/1.0.0-rc3.2",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);

    if (!session) return false;

    HINTERNET connect = WinHttpConnect(
        session,
        L"api.truckersmp.com",
        INTERNET_DEFAULT_HTTPS_PORT,
        0);

    if (!connect) {
        WinHttpCloseHandle(session);
        return false;
    }

    std::wstring path = L"/v2/player/" + std::to_wstring(accountId);

    HINTERNET request = WinHttpOpenRequest(
        connect,
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);

    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    WinHttpSetTimeouts(request, 2000, 2000, 2000, 4000);

    BOOL ok = WinHttpSendRequest(
        request,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0);

    if (ok) ok = WinHttpReceiveResponse(request, nullptr);

    DWORD status = 0;
    DWORD statusSize = sizeof(status);

    if (ok) {
        WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &statusSize,
            WINHTTP_NO_HEADER_INDEX);
    }

    std::string body;

    if (ok && status == 200) {
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;

            const size_t oldSize = body.size();
            body.resize(oldSize + available);

            DWORD bytesRead = 0;
            if (!WinHttpReadData(request, body.data() + oldSize, available, &bytesRead)) {
                body.clear();
                break;
            }

            body.resize(oldSize + bytesRead);
        }
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    return !body.empty() && JsonString(body, "groupName", groupName);
}

static void QueueLookup(uint64_t accountId) {
    if (!accountId) return;

    std::lock_guard<std::mutex> lock(g_apiMutex);

    if (g_resolvedAccounts.count(accountId) || g_pending.count(accountId)) return;

    auto retry = g_retryAfter.find(accountId);
    if (retry != g_retryAfter.end() && Clock::now() < retry->second) return;

    g_pending.insert(accountId);
    g_lookupQueue.push(accountId);
    g_apiCv.notify_one();
}

static bool GetStaffColor(uint64_t accountId, RGB& color) {
    std::lock_guard<std::mutex> lock(g_apiMutex);

    auto it = g_staffColorByAccount.find(accountId);
    if (it == g_staffColorByAccount.end()) return false;

    color = it->second;
    return true;
}

static void ApiWorker() {
    while (!g_stop.load()) {
        uint64_t accountId = 0;

        {
            std::unique_lock<std::mutex> lock(g_apiMutex);

            g_apiCv.wait(lock, [] {
                return g_stop.load() || !g_lookupQueue.empty();
            });

            if (g_stop.load()) break;

            accountId = g_lookupQueue.front();
            g_lookupQueue.pop();
        }

        std::string groupName;
        const bool fetched = FetchGroupName(accountId, groupName);

        bool isMappedStaff = false;
        RGB color{};

        if (fetched) {
            auto it = g_roleColors.find(Lower(groupName));
            if (it != g_roleColors.end()) {
                color = it->second;
                isMappedStaff = true;
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_apiMutex);

            g_pending.erase(accountId);

            if (fetched) {
                g_resolvedAccounts.insert(accountId);

                if (isMappedStaff) {
                    g_staffColorByAccount[accountId] = color;
                }
            } else {
                g_retryAfter[accountId] = Clock::now() + std::chrono::seconds(15);
            }
        }

        if (fetched) {
            Log(
                "TMPID " + std::to_string(accountId) +
                " groupName=" + groupName +
                (isMappedStaff ? " [STAFF COLOR MAPPED]" : " [normal/unmapped]")
            );
        }
    }
}

// -----------------------------------------------------------------------------
// Visible players
// -----------------------------------------------------------------------------

static std::vector<VisiblePlayer> g_visiblePlayers;
static Clock::time_point g_lastPlayerRefresh{};

static void RefreshVisiblePlayers() {
    const auto now = Clock::now();

    if (g_lastPlayerRefresh.time_since_epoch().count() != 0 &&
        now - g_lastPlayerRefresh < std::chrono::milliseconds(200)) {
        return;
    }

    g_lastPlayerRefresh = now;

    std::vector<VisiblePlayer> rows;

    const uint64_t localAccount =
        g_session->Account().GetAccountID().value_or(0);

    auto local = g_session->Player().GetLocalPlayer();

    if (local && local->IsValid()) {
        VisiblePlayer p;
        p.local = true;
        p.playerId = local->GetPlayerID().value_or(0);
        p.accountId = localAccount
            ? localAccount
            : local->GetAccountID().value_or(0);
        p.username = local->GetUsername().value_or("");

        rows.push_back(p);
        QueueLookup(p.accountId);
    }

    std::vector<VisiblePlayer> remote;

    auto all = g_session->Player().GetAllPlayers();

    if (all) {
        for (const auto& player : *all) {
            if (!player.IsValid()) continue;

            VisiblePlayer p;
            p.playerId = player.GetPlayerID().value_or(0);
            p.accountId = player.GetAccountID().value_or(0);
            p.username = player.GetUsername().value_or("");

            if (localAccount && p.accountId == localAccount) continue;

            if (local && local->IsValid() &&
                p.playerId == local->GetPlayerID().value_or(-123456)) {
                continue;
            }

            remote.push_back(p);
            QueueLookup(p.accountId);
        }
    }

    std::sort(
        remote.begin(),
        remote.end(),
        [](const VisiblePlayer& a, const VisiblePlayer& b) {
            return a.playerId < b.playerId;
        });

    rows.insert(rows.end(), remote.begin(), remote.end());

    g_visiblePlayers = std::move(rows);
}

// -----------------------------------------------------------------------------
// Chat log support
// -----------------------------------------------------------------------------

static fs::path g_chatLogPath;
static uintmax_t g_chatOffset=0;
static Clock::time_point g_lastChatPoll{};
static Clock::time_point g_lastChatDiscovery{};
static Clock::time_point g_localChatFallbackUntil{};
static bool g_enterWasDown=false;
static std::deque<ChatStaffEvent> g_recentChatEvents;
static std::unordered_map<std::string, RGB> g_teamColorByUsername;
static std::unordered_map<int, RGB> g_teamColorByPlayerId;
static bool g_expectTeamListLine=false;
static int g_lastChatMarkerDebug=-1;
static int g_lastChatColorDebug=-1;

static fs::path DocumentsPath() {
    PWSTR wide=nullptr;
    fs::path out;

    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &wide)) && wide) {
        out=fs::path(wide);
        CoTaskMemFree(wide);
    }

    return out;
}


static std::vector<fs::path> TmpHomeCandidates() {
    std::vector<fs::path> roots;

    fs::path docs=DocumentsPath();
    if(!docs.empty()) {
        roots.push_back(docs/"ETS2MP");
        roots.push_back(docs/"ATSMP");
    }

    if(const wchar_t* oneDrive=_wgetenv(L"OneDrive")) {
        fs::path od(oneDrive);
        roots.push_back(od/"Documenti"/"ETS2MP");
        roots.push_back(od/"Documents"/"ETS2MP");
        roots.push_back(od/"Documenti"/"ATSMP");
        roots.push_back(od/"Documents"/"ATSMP");
    }

    if(const wchar_t* profile=_wgetenv(L"USERPROFILE")) {
        fs::path up(profile);
        roots.push_back(up/"Documents"/"ETS2MP");
        roots.push_back(up/"Documenti"/"ETS2MP");
    }

    return roots;
}

static bool JsonIntAfterKey(
    const std::string& data,
    const std::string& key,
    int& out) {

    const std::string lower=Lower(data);
    const std::string needle="\""+Lower(key)+"\"";

    size_t p=lower.find(needle);
    if(p==std::string::npos) return false;

    p=lower.find(':',p+needle.size());
    if(p==std::string::npos) return false;

    ++p;
    while(p<data.size() && std::isspace((unsigned char)data[p])) ++p;

    bool neg=false;
    if(p<data.size() && data[p]=='-') {
        neg=true;
        ++p;
    }

    size_t b=p;
    while(p<data.size() && std::isdigit((unsigned char)data[p])) ++p;
    if(p==b) return false;

    try {
        int v=std::stoi(data.substr(b,p-b));
        out=neg ? -v : v;
        return true;
    } catch(...) {
        return false;
    }
}

static void RefreshLocalGroupVisibilityFromConfig() {
    const auto now=Clock::now();

    if(g_lastGroupConfigPoll.time_since_epoch().count()!=0 &&
       now-g_lastGroupConfigPoll<std::chrono::milliseconds(100)) {
        return;
    }

    g_lastGroupConfigPoll=now;

    for(const auto& root:TmpHomeCandidates()) {
        const fs::path cfg=root/"config.txt";

        std::ifstream f(cfg,std::ios::binary);
        if(!f) continue;

        std::string data(
            (std::istreambuf_iterator<char>(f)),
            std::istreambuf_iterator<char>());

        const std::string lower=Lower(data);

        // Toggle Group.
        const size_t p=lower.find("\"display_group\"");

        if(p!=std::string::npos) {
            const size_t colon=lower.find(':',p);

            if(colon!=std::string::npos) {
                const size_t t=lower.find("true",colon);
                const size_t ff=lower.find("false",colon);

                if(t!=std::string::npos &&
                   (ff==std::string::npos || t<ff)) {
                    g_localGroupVisible=true;
                    g_localGroupVisibilityKnown=true;
                } else if(ff!=std::string::npos) {
                    g_localGroupVisible=false;
                    g_localGroupVisibilityKnown=true;
                }
            }
        }

        // LIVE Player Panel rectangle.
        //
        // This is critical because History also contains the native staff
        // shield texture.  Previous builds treated every non-chat shield as a
        // live TAB row, so a History shield could inherit the first live staff
        // colour (usually the local Community Moderator colour).
        int x=0,y=0,w=0,h=0;

        if(JsonIntAfterKey(data,"position_x",x) &&
           JsonIntAfterKey(data,"position_y",y) &&
           JsonIntAfterKey(data,"size_x",w) &&
           JsonIntAfterKey(data,"size_y",h) &&
           w>0 && h>0) {

            g_playerPanelGeometry.x=x;
            g_playerPanelGeometry.y=y;
            g_playerPanelGeometry.w=w;
            g_playerPanelGeometry.h=h;
            g_playerPanelGeometry.valid=true;
        }

        return;
    }
}

static bool IsInsideLivePlayerPanel(
    const MarkerRect& m,
    UINT width,
    UINT height) {

    if(!g_playerPanelGeometry.valid) {
        // Fail safe: if TMP config is temporarily unavailable, keep the old
        // behaviour rather than breaking the live TAB completely.
        return true;
    }

    // TMP config is normally in UI/backbuffer coordinates. Add a small margin
    // because the shield can sit close to the panel border.
    const int margin=24;

    const int left=std::max(0,g_playerPanelGeometry.x-margin);
    const int top=std::max(0,g_playerPanelGeometry.y-margin);
    const int right=std::min(
        (int)width,
        g_playerPanelGeometry.x+g_playerPanelGeometry.w+margin);
    const int bottom=std::min(
        (int)height,
        g_playerPanelGeometry.y+g_playerPanelGeometry.h+margin);

    const int cx=m.x+m.w/2;
    const int cy=m.y+m.h/2;

    return cx>=left && cx<right && cy>=top && cy<bottom;
}

static fs::path FindNewestChatLog() {
    std::vector<fs::path> roots;

    // Standard redirected Documents folder (works for OneDrive Documents too
    // on most Windows installs).
    fs::path docs=DocumentsPath();
    if(!docs.empty()) {
        roots.push_back(docs/"ETS2MP"/"logs");
        roots.push_back(docs/"ATSMP"/"logs");
    }

    // Explicit OneDrive fallbacks.  The real TMP client log on this machine
    // reports: C:\Users\<user>\OneDrive\Documenti\ETS2MP
    if(const wchar_t* oneDrive=_wgetenv(L"OneDrive")) {
        fs::path od(oneDrive);
        roots.push_back(od/"Documenti"/"ETS2MP"/"logs");
        roots.push_back(od/"Documents"/"ETS2MP"/"logs");
        roots.push_back(od/"Documenti"/"ATSMP"/"logs");
        roots.push_back(od/"Documents"/"ATSMP"/"logs");
    }

    // User-profile fallbacks.
    if(const wchar_t* profile=_wgetenv(L"USERPROFILE")) {
        fs::path up(profile);
        roots.push_back(up/"Documents"/"ETS2MP"/"logs");
        roots.push_back(up/"Documenti"/"ETS2MP"/"logs");
    }

    fs::path best;
    fs::file_time_type bestTime{};

    for(const auto& dir:roots) {
        std::error_code ec;
        if(!fs::exists(dir,ec)) continue;

        for(const auto& entry:fs::directory_iterator(dir,ec)) {
            if(ec || !entry.is_regular_file()) continue;

            const std::string name=Lower(entry.path().filename().string());

            // REAL TruckersMP naming:
            // chat_2026_09_04_log.txt
            if(name.rfind("chat_",0)!=0) continue;
            if(name.find("_log.txt")==std::string::npos) continue;

            auto t=entry.last_write_time(ec);
            if(ec) continue;

            if(best.empty() || t>bestTime) {
                best=entry.path();
                bestTime=t;
            }
        }
    }

    return best;
}

static bool ColorFromRoleText(const std::string& lowerLine, RGB& out) {
    // g_rolesByPriority is highest role -> lowest role.
    // First match therefore implements "highest role wins".
    for (const auto& role : g_rolesByPriority) {
        if (lowerLine.find(role.first) != std::string::npos) {
            out=role.second;
            return true;
        }
    }
    return false;
}

static bool ColorFromVisibleUsername(const std::string& lowerLine, RGB& out) {
    // Longest name match wins to reduce substring collisions.
    size_t bestLen=0;
    RGB best{};
    bool found=false;

    for (const auto& player : g_visiblePlayers) {
        if (player.username.empty()) continue;

        const std::string nameLower=Lower(player.username);
        if (nameLower.size()<=bestLen) continue;

        if (lowerLine.find(nameLower)!=std::string::npos) {
            RGB c;
            if (GetStaffColor(player.accountId,c)) {
                best=c;
                bestLen=nameLower.size();
                found=true;
            }
        }
    }

    if (found) out=best;
    return found;
}

static void ParseTeamMembersOnlineLine(const std::string& line) {
    // Supported real TMP forms:
    //
    //   * Game Moderator wenzy
    //   * Game Moderator wenzy (431)
    //   * Game Moderator [MCG] Valhalla (21)
    //
    // The temporary/player ID is the most reliable key because VTC tags or
    // display formatting can make the visible name differ from the SDK name.

    const std::string lower=Lower(line);

    for(size_t roleIndex=0; roleIndex<g_rolesByPriority.size(); ++roleIndex) {
        const std::string& role=g_rolesByPriority[roleIndex].first;
        const RGB color=g_rolesByPriority[roleIndex].second;

        size_t pos=0;

        while((pos=lower.find(role,pos))!=std::string::npos) {
            size_t nameStart=pos+role.size();

            while(nameStart<line.size() &&
                  std::isspace((unsigned char)line[nameStart])) {
                ++nameStart;
            }

            size_t nameEnd=line.find(" * ",nameStart);
            if(nameEnd==std::string::npos) nameEnd=line.size();

            std::string username=line.substr(nameStart,nameEnd-nameStart);

            while(!username.empty() &&
                  std::isspace((unsigned char)username.back())) {
                username.pop_back();
            }

            while(!username.empty() &&
                  std::isspace((unsigned char)username.front())) {
                username.erase(username.begin());
            }

            // Optional TMP temporary/player ID at the end: "(21)".
            int playerId=-1;

            if(!username.empty() && username.back()==')') {
                const size_t lp=username.rfind('(');

                if(lp!=std::string::npos && lp+1<username.size()-1) {
                    const std::string idText=
                        username.substr(lp+1,username.size()-lp-2);

                    bool allDigits=!idText.empty();

                    for(char ch:idText) {
                        if(!std::isdigit((unsigned char)ch)) {
                            allDigits=false;
                            break;
                        }
                    }

                    if(allDigits) {
                        try {
                            playerId=std::stoi(idText);
                        } catch(...) {
                            playerId=-1;
                        }

                        username.erase(lp);

                        while(!username.empty() &&
                              std::isspace((unsigned char)username.back())) {
                            username.pop_back();
                        }
                    }
                }
            }

            if(playerId>=0) {
                g_teamColorByPlayerId[playerId]=color;

                Log(
                    "TEAM ROLE ID "+
                    std::to_string(playerId)+
                    " -> "+role
                );
            }

            if(!username.empty()) {
                g_teamColorByUsername[Lower(username)]=color;
                Log("TEAM ROLE "+username+" -> "+role);
            }

            pos=nameEnd;
        }
    }
}

static bool ColorFromTeamPlayerId(int playerId, RGB& out) {
    if(playerId<0) return false;

    auto it=g_teamColorByPlayerId.find(playerId);
    if(it==g_teamColorByPlayerId.end()) return false;

    out=it->second;
    return true;
}

static bool ColorFromTeamUsername(const std::string& username, RGB& out) {
    auto it=g_teamColorByUsername.find(Lower(username));
    if(it==g_teamColorByUsername.end()) return false;
    out=it->second;
    return true;
}

static std::string SenderUsername(const std::string& line) {
    // Strip "[Global] [HH:MM:SS] " then take everything before the first " ("
    size_t start=0;

    if(line.rfind("[Global]",0)==0) {
        size_t secondClose=line.find(']',9);
        if(secondClose!=std::string::npos) {
            start=secondClose+1;
            while(start<line.size() && std::isspace((unsigned char)line[start])) ++start;
        }
    }

    size_t paren=line.find(" (",start);
    if(paren==std::string::npos) return {};

    std::string name=line.substr(start,paren-start);

    while(!name.empty() && std::isspace((unsigned char)name.back())) name.pop_back();
    while(!name.empty() && std::isspace((unsigned char)name.front())) name.erase(name.begin());

    return name;
}

static int FirstSenderPlayerId(const std::string& line) {
    // TMP rendered/chat lines use the sender block first:
    //   name (415):
    //   name (Community Moderator 415):
    // Mentions later in the message may contain more "(123)" blocks, so only
    // inspect the FIRST parenthesized block.
    const auto l=line.find('(');
    if(l==std::string::npos) return -1;

    const auto r=line.find(')',l+1);
    if(r==std::string::npos) return -1;

    const std::string block=line.substr(l+1,r-l-1);

    int last=-1;
    size_t i=0;

    while(i<block.size()) {
        while(i<block.size() && !std::isdigit((unsigned char)block[i])) ++i;
        if(i>=block.size()) break;

        size_t j=i;
        while(j<block.size() && std::isdigit((unsigned char)block[j])) ++j;

        try {
            last=std::stoi(block.substr(i,j-i));
        } catch(...) {}

        i=j;
    }

    return last;
}

static bool ColorFromPlayerId(int playerId, RGB& out) {
    if(playerId<0) return false;

    for(const auto& player:g_visiblePlayers) {
        if(player.playerId!=playerId) continue;

        if(GetStaffColor(player.accountId,out)) {
            return true;
        }

        // The API may still be resolving. Make sure it is queued.
        QueueLookup(player.accountId);
        return false;
    }

    return false;
}

static void PushChatEvent(const RGB& c,int playerId) {
    const auto now=Clock::now();

    // Deduplicate the exact same staff sender/color if the file writer repeats
    // a line immediately.
    if(!g_recentChatEvents.empty()) {
        const auto& last=g_recentChatEvents.back();

        if(last.playerId==playerId &&
           last.color==c &&
           now-last.when<std::chrono::milliseconds(80)) {
            return;
        }
    }

    g_recentChatEvents.push_back({c,playerId,now});
}

static void ParseChatLine(const std::string& line) {
    if(line.empty()) return;

    const std::string lowerLine=Lower(line);

    // Respect TMP Toggle Group for the local staff member.
    // If TMP hides the group, the native shield disappears and the TAB
    // nickname must stay untouched too.
    if(lowerLine.find("[system] your group is now hidden.")!=std::string::npos) {
        g_localGroupVisible=false;
        g_localGroupVisibilityKnown=true;
        Log("LOCAL GROUP: hidden");
        return;
    }

    if(lowerLine.find("[system] your group is now visible.")!=std::string::npos) {
        g_localGroupVisible=true;
        g_localGroupVisibilityKnown=true;
        Log("LOCAL GROUP: visible");
        return;
    }

    // The first system line announces that the NEXT line contains the actual
    // online staff roles/usernames.
    if(lowerLine.find("[system] team members online:")!=std::string::npos) {
        g_expectTeamListLine=true;
        return;
    }

    if(g_expectTeamListLine) {
        g_expectTeamListLine=false;

        if(line.find("* ")!=std::string::npos) {
            g_teamColorByUsername.clear();
            g_teamColorByPlayerId.clear();
            ParseTeamMembersOnlineLine(line);
            return;
        }
    }

    const int playerId=FirstSenderPlayerId(line);
    const std::string username=SenderUsername(line);

    if(username.empty()) return;

    RGB c;

    // BEST source for normal chat: system-generated "Team members online"
    // mapping. This contains the exact role TMP displays, unlike the chat line.
    if(ColorFromTeamUsername(username,c)) {
        PushChatEvent(c,playerId);
        return;
    }

    // Fallback: visible player id -> API.
    if(ColorFromPlayerId(playerId,c)) {
        PushChatEvent(c,playerId);
        return;
    }

    // Last fallback: visible username -> API.
    if(ColorFromVisibleUsername(lowerLine,c)) {
        PushChatEvent(c,playerId);
    }
}


static bool LocalStaffColor(RGB& out);

static void UpdateLocalChatSendArm() {
    const bool down=
        (GetAsyncKeyState(VK_RETURN)&0x8000)!=0;

    // Arm a very short local-chat fallback on ENTER press.
    // This covers the frame in which TMP renders the new local staff shield
    // before its chat line has physically reached chat_*_log.txt.
    if(down && !g_enterWasDown) {
        RGB local{};

        if(LocalStaffColor(local) &&
           (!g_localGroupVisibilityKnown || g_localGroupVisible)) {

            g_localChatFallbackUntil=
                Clock::now()+std::chrono::milliseconds(180);
        }
    }

    g_enterWasDown=down;
}

static void PollChatLog() {
    const auto now=Clock::now();

    // RC2 low-latency chat path:
    // tail the already-open chat log on practically every rendered frame.
    // The old 60 ms throttle was directly visible as a short pink/white flash
    // when a new staff message appeared.
    if (g_lastChatPoll.time_since_epoch().count()!=0 &&
        now-g_lastChatPoll<std::chrono::milliseconds(8)) {
        return;
    }

    g_lastChatPoll=now;

    fs::path newest=g_chatLogPath;

    // Directory discovery is much more expensive than checking the current
    // file size, so only rescan periodically (or immediately if no file is
    // known). This keeps 8 ms tail polling cheap.
    const bool rediscover=
        newest.empty() ||
        !fs::exists(newest) ||
        g_lastChatDiscovery.time_since_epoch().count()==0 ||
        now-g_lastChatDiscovery>std::chrono::seconds(2);

    if(rediscover) {
        fs::path discovered=FindNewestChatLog();
        g_lastChatDiscovery=now;

        if(!discovered.empty()) {
            newest=discovered;
        }
    }

    if (newest.empty()) return;

    std::error_code ec;
    uintmax_t size=fs::file_size(newest,ec);
    if (ec) return;

    if (g_chatLogPath!=newest) {
        g_chatLogPath=newest;

        // Read a small tail so already-visible chat messages can be recognized
        // immediately after the plugin starts.
        g_chatOffset = size>65536 ? size-65536 : 0;
        g_teamColorByUsername.clear();
        g_teamColorByPlayerId.clear();
        g_expectTeamListLine=false;
        Log("Chat log FOUND: "+g_chatLogPath.string());
    }

    if (size<g_chatOffset) {
        g_chatOffset=0;
    }

    if (size==g_chatOffset) return;

    std::ifstream f(g_chatLogPath,std::ios::binary);
    if (!f) return;

    f.seekg((std::streamoff)g_chatOffset,std::ios::beg);

    std::string data(
        (std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>());

    g_chatOffset=size;

    // If we started mid-file, the first piece may be half a line.
    size_t start=0;
    while (start<data.size()) {
        size_t end=data.find('\n',start);
        if (end==std::string::npos) end=data.size();

        std::string line=data.substr(start,end-start);
        if (!line.empty() && line.back()=='\r') line.pop_back();

        ParseChatLine(line);

        start=end+1;
    }

    // Old chat lines no longer visible should not influence future marker mapping.
    while (!g_recentChatEvents.empty() &&
           now-g_recentChatEvents.front().when>std::chrono::minutes(10)) {
        g_recentChatEvents.pop_front();
    }
}

// -----------------------------------------------------------------------------
// DirectX readback + marker detector
// -----------------------------------------------------------------------------

static ID3D11Device* g_device=nullptr;
static ID3D11DeviceContext* g_ctx=nullptr;

struct ReadbackSlot {
    ID3D11Texture2D* resolveTex=nullptr;
    ID3D11Texture2D* stagingTex=nullptr;
    UINT width=0;
    UINT height=0;
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    bool pending=false;
};

static constexpr int kReadbackSlots=4;
static ReadbackSlot g_slots[kReadbackSlots];
static int g_writeSlot=0;
static Clock::time_point g_lastReadbackSubmit{};
static std::vector<MarkerRect> g_markers;
static int g_lastLoggedMarkerCount=-1;
static int g_lastLoggedFirstX=-99999;
static int g_lastLoggedFirstY=-99999;

static bool IsSupportedFormat(DXGI_FORMAT f) {
    return
        f==DXGI_FORMAT_R8G8B8A8_UNORM ||
        f==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
        f==DXGI_FORMAT_B8G8R8A8_UNORM ||
        f==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
}

static void ReadRGB(
    const uint8_t* p,
    DXGI_FORMAT f,
    uint8_t& r,
    uint8_t& g,
    uint8_t& b) {

    if (f==DXGI_FORMAT_B8G8R8A8_UNORM ||
        f==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {

        b=p[0];
        g=p[1];
        r=p[2];
    } else {
        r=p[0];
        g=p[1];
        b=p[2];
    }
}

static bool IsMagentaMarkerPixel(
    const uint8_t* p,
    DXGI_FORMAT format) {

    uint8_t r=0,g=0,b=0;
    ReadRGB(p,format,r,g,b);

    return
        r>=210 &&
        b>=210 &&
        g<=65 &&
        ((int)std::min(r,b)-(int)g)>=155;
}

static void DestroySlot(ReadbackSlot& s) {
    Rel(s.resolveTex);
    Rel(s.stagingTex);
    s.width=0;
    s.height=0;
    s.format=DXGI_FORMAT_UNKNOWN;
    s.pending=false;
}

static bool EnsureSlot(
    ReadbackSlot& slot,
    const D3D11_TEXTURE2D_DESC& backDesc) {

    if (slot.stagingTex &&
        slot.width==backDesc.Width &&
        slot.height==backDesc.Height &&
        slot.format==backDesc.Format) {
        return true;
    }

    DestroySlot(slot);

    if (!IsSupportedFormat(backDesc.Format)) return false;

    if (backDesc.SampleDesc.Count>1) {
        D3D11_TEXTURE2D_DESC resolveDesc{};
        resolveDesc.Width=backDesc.Width;
        resolveDesc.Height=backDesc.Height;
        resolveDesc.MipLevels=1;
        resolveDesc.ArraySize=1;
        resolveDesc.Format=backDesc.Format;
        resolveDesc.SampleDesc.Count=1;
        resolveDesc.Usage=D3D11_USAGE_DEFAULT;

        if (FAILED(g_device->CreateTexture2D(
                &resolveDesc,
                nullptr,
                &slot.resolveTex))) {
            return false;
        }
    }

    D3D11_TEXTURE2D_DESC stageDesc{};
    stageDesc.Width=backDesc.Width;
    stageDesc.Height=backDesc.Height;
    stageDesc.MipLevels=1;
    stageDesc.ArraySize=1;
    stageDesc.Format=backDesc.Format;
    stageDesc.SampleDesc.Count=1;
    stageDesc.Usage=D3D11_USAGE_STAGING;
    stageDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;

    if (FAILED(g_device->CreateTexture2D(
            &stageDesc,
            nullptr,
            &slot.stagingTex))) {

        DestroySlot(slot);
        return false;
    }

    slot.width=backDesc.Width;
    slot.height=backDesc.Height;
    slot.format=backDesc.Format;

    return true;
}

static void DetectMarkersFromMapped(
    const D3D11_MAPPED_SUBRESOURCE& mapped,
    UINT width,
    UINT height,
    DXGI_FORMAT format) {

    std::vector<uint8_t> mask((size_t)width*(size_t)height,0);

    for (UINT y=0;y<height;++y) {
        const uint8_t* row=
            (const uint8_t*)mapped.pData+
            (size_t)y*mapped.RowPitch;

        for (UINT x=0;x<width;++x) {
            if (IsMagentaMarkerPixel(row+(size_t)x*4,format)) {
                mask[(size_t)y*width+x]=1;
            }
        }
    }

    std::vector<MarkerRect> found;
    std::vector<uint32_t> queue;
    queue.reserve(2048);

    for (UINT y=0;y<height;++y) {
        for (UINT x=0;x<width;++x) {
            const size_t start=(size_t)y*width+x;
            if (mask[start]!=1) continue;

            int minX=(int)x,maxX=(int)x;
            int minY=(int)y,maxY=(int)y;
            int count=0;

            queue.clear();
            queue.push_back((uint32_t)start);
            mask[start]=2;

            for (size_t qi=0;qi<queue.size();++qi) {
                const uint32_t cur=queue[qi];
                const int cy=(int)(cur/width);
                const int cx=(int)(cur-(uint32_t)cy*width);

                ++count;

                minX=std::min(minX,cx);
                maxX=std::max(maxX,cx);
                minY=std::min(minY,cy);
                maxY=std::max(maxY,cy);

                for (int dy=-1;dy<=1;++dy) {
                    for (int dx=-1;dx<=1;++dx) {
                        if (dx==0 && dy==0) continue;

                        const int nx=cx+dx;
                        const int ny=cy+dy;

                        if (nx<0 || ny<0 ||
                            nx>=(int)width ||
                            ny>=(int)height) {
                            continue;
                        }

                        const size_t ni=(size_t)ny*width+(size_t)nx;

                        if (mask[ni]==1) {
                            mask[ni]=2;
                            queue.push_back((uint32_t)ni);
                        }
                    }
                }
            }

            const int bw=maxX-minX+1;
            const int bh=maxY-minY+1;

            if (count<16) continue;
            if (bw<5 || bh<5) continue;
            if (bw>80 || bh>80) continue;

            const float aspect=(float)bw/(float)bh;
            if (aspect<0.45f || aspect>1.40f) continue;

            MarkerRect m;
            m.x=std::max(0,minX-2);
            m.y=std::max(0,minY-2);
            m.w=std::min((int)width-m.x,bw+4);
            m.h=std::min((int)height-m.y,bh+4);

            found.push_back(m);
        }
    }

    std::sort(
        found.begin(),
        found.end(),
        [](const MarkerRect& a,const MarkerRect& b) {
            if (std::abs(a.y-b.y)>3) return a.y<b.y;
            return a.x<b.x;
        });

    std::vector<MarkerRect> clean;

    for (const auto& m:found) {
        bool duplicate=false;

        for (auto& c:clean) {
            const int acx=m.x+m.w/2;
            const int acy=m.y+m.h/2;
            const int bcx=c.x+c.w/2;
            const int bcy=c.y+c.h/2;

            if (std::abs(acx-bcx)<12 &&
                std::abs(acy-bcy)<12) {

                const int x1=std::min(c.x,m.x);
                const int y1=std::min(c.y,m.y);
                const int x2=std::max(c.x+c.w,m.x+m.w);
                const int y2=std::max(c.y+c.h,m.y+m.h);

                c.x=x1;
                c.y=y1;
                c.w=x2-x1;
                c.h=y2-y1;

                duplicate=true;
                break;
            }
        }

        if (!duplicate) clean.push_back(m);
    }

    g_markers=std::move(clean);

    const int firstX=g_markers.empty() ? -1 : g_markers.front().x;
    const int firstY=g_markers.empty() ? -1 : g_markers.front().y;

    if ((int)g_markers.size()!=g_lastLoggedMarkerCount ||
        std::abs(firstX-g_lastLoggedFirstX)>4 ||
        std::abs(firstY-g_lastLoggedFirstY)>4) {

        Log(
            "Detected "+std::to_string(g_markers.size())+
            " native magenta marker(s)"+
            (g_markers.empty()
                ? std::string("")
                : " first=("+std::to_string(firstX)+","+std::to_string(firstY)+")")
        );

        g_lastLoggedMarkerCount=(int)g_markers.size();
        g_lastLoggedFirstX=firstX;
        g_lastLoggedFirstY=firstY;
    }
}

static void ProcessReadyReadbacks() {
    // Try every pending slot. DO_NOT_WAIT means this never blocks the render thread.
    for (int i=0;i<kReadbackSlots;++i) {
        ReadbackSlot& slot=g_slots[i];

        if (!slot.pending || !slot.stagingTex) continue;

        D3D11_MAPPED_SUBRESOURCE mapped{};

        HRESULT hr=g_ctx->Map(
            slot.stagingTex,
            0,
            D3D11_MAP_READ,
            D3D11_MAP_FLAG_DO_NOT_WAIT,
            &mapped);

        if (hr==DXGI_ERROR_WAS_STILL_DRAWING) continue;

        if (FAILED(hr)) {
            slot.pending=false;
            continue;
        }

        DetectMarkersFromMapped(
            mapped,
            slot.width,
            slot.height,
            slot.format);

        g_ctx->Unmap(slot.stagingTex,0);
        slot.pending=false;
    }
}

static void SubmitReadback(
    ID3D11Texture2D* backbuffer,
    const D3D11_TEXTURE2D_DESC& desc) {

    const auto now=Clock::now();

    // RC2 FAST PATH:
    // no fixed 30 ms throttle. We submit whenever a staging slot is free.
    // The GPU/available-slot pipeline becomes the limiter, so moving TAB/chat
    // can be picked up on the next available rendered frame instead of waiting
    // for an artificial timer.
    g_lastReadbackSubmit=now;

    // Find a free slot rather than overwriting an in-flight copy.
    int chosen=-1;

    for (int n=0;n<kReadbackSlots;++n) {
        int idx=(g_writeSlot+n)%kReadbackSlots;
        if (!g_slots[idx].pending) {
            chosen=idx;
            break;
        }
    }

    if (chosen<0) return;

    ReadbackSlot& slot=g_slots[chosen];

    if (!EnsureSlot(slot,desc)) return;

    if (desc.SampleDesc.Count>1) {
        g_ctx->ResolveSubresource(
            slot.resolveTex,
            0,
            backbuffer,
            0,
            desc.Format);

        g_ctx->CopyResource(
            slot.stagingTex,
            slot.resolveTex);
    } else {
        g_ctx->CopyResource(
            slot.stagingTex,
            backbuffer);
    }

    slot.pending=true;
    g_writeSlot=(chosen+1)%kReadbackSlots;
}

// -----------------------------------------------------------------------------
// GPU recolor
// -----------------------------------------------------------------------------

static ID3D11Texture2D* g_frameCopy=nullptr;
static ID3D11ShaderResourceView* g_frameSrv=nullptr;
static UINT g_frameW=0;
static UINT g_frameH=0;
static DXGI_FORMAT g_frameFormat=DXGI_FORMAT_UNKNOWN;

static ID3D11VertexShader* g_vs=nullptr;
static ID3D11PixelShader* g_ps=nullptr;
static ID3D11Buffer* g_cb=nullptr;
static ID3D11BlendState* g_blend=nullptr;
static ID3D11DepthStencilState* g_depth=nullptr;
static ID3D11RasterizerState* g_raster=nullptr;

struct RectColor {
    float rect[4];
    float color[4];
    float meta[4];   // x: mode; nickname: y/z = shield anchor center, w = anchor radius
};

struct ShaderCB {
    float viewport[4];      // width, height, rectCount, chatRight/fallbackScope
    float fallback[4];      // panel anti-flicker fallback
    float chatFallback[4];  // newest/local chat anti-flash fallback
};

static ID3D11Buffer* g_rectBuffer=nullptr;
static ID3D11ShaderResourceView* g_rectSrv=nullptr;
static UINT g_rectCapacity=0;

static bool CompileShaders() {
    const char* vsSource=R"(
struct VSOut {
    float4 pos : SV_Position;
};

VSOut main(uint id : SV_VertexID) {
    float2 p;

    if (id==0)      p=float2(-1.0,-1.0);
    else if (id==1) p=float2(-1.0, 3.0);
    else            p=float2( 3.0,-1.0);

    VSOut o;
    o.pos=float4(p,0.0,1.0);
    return o;
})";

    const char* psSource=R"(
Texture2D<float4> Source : register(t0);

struct RectColor {
    float4 rect;
    float4 color;
    float4 meta; // x=mode, yz=shield anchor center, w=anchor radius
};

StructuredBuffer<RectColor> items : register(t1);

cbuffer MarkerCB : register(b0) {
    float4 viewport;
    float4 fallback;
    float4 chatFallback;
};

float4 main(float4 pos : SV_Position) : SV_Target {
    int2 xy=int2(pos.xy);
    float4 src=Source.Load(int3(xy,0));

    float magentaStrength=min(src.r,src.b)-src.g;

    bool markerPixel=
        src.r>0.52 &&
        src.b>0.52 &&
        magentaStrength>0.20;

    int count=(int)viewport.z;

    [loop]
    for (int i=0;i<count;++i) {
        float4 rc=items[i].rect;

        if (!(pos.x>=rc.x && pos.x<rc.z &&
              pos.y>=rc.y && pos.y<rc.w &&
              items[i].color.a>0.5)) {
            continue;
        }

        // mode 0 = native magenta staff shield.
        if(items[i].meta.x<0.5) {
            if(!markerPixel) continue;

            float strength=saturate((magentaStrength-0.16)/0.84);

            src.rgb=lerp(
                src.rgb,
                items[i].color.rgb,
                strength);

            return src;
        }

        // mode 1 = TAB nickname with shield-anchor guard.
        // mode 2 = TAB nickname while the panel is actively being dragged.
        //
        // During a known drag the rectangle is translated from the mouse delta
        // every frame, so waiting for the magenta anchor is counterproductive:
        // TMP can move the icon a rounded pixel before the sampled anchor and
        // the nickname flashes white for one frame.
        //
        // MOTION GUARD 0.8.5:
        // A nickname rectangle is allowed to recolour pixels ONLY while the
        // native magenta shield still exists at the anchor position that was
        // detected for that same row. When TAB moves/scrolls, the old absolute
        // rectangle immediately becomes stale; without this guard it can paint
        // random bright UI/background pixels until the next async readback.
        //
        // We sample a small cross around the shield center. The marker texture
        // contains enough magenta area that at least one sample should hit while
        // the row is still at the expected position.
        if(items[i].meta.x<1.5) {
            int2 ac=int2(items[i].meta.yz);
            int ar=max(2,(int)items[i].meta.w);

            uint aw=0,ah=0;
            Source.GetDimensions(aw,ah);

            int2 a0=int2(clamp(ac.x,0,(int)aw-1),clamp(ac.y,0,(int)ah-1));
            int2 a1=int2(clamp(ac.x-ar,0,(int)aw-1),a0.y);
            int2 a2=int2(clamp(ac.x+ar,0,(int)aw-1),a0.y);
            int2 a3=int2(a0.x,clamp(ac.y-ar,0,(int)ah-1));
            int2 a4=int2(a0.x,clamp(ac.y+ar,0,(int)ah-1));

            float3 s0=Source.Load(int3(a0,0)).rgb;
            float3 s1=Source.Load(int3(a1,0)).rgb;
            float3 s2=Source.Load(int3(a2,0)).rgb;
            float3 s3=Source.Load(int3(a3,0)).rgb;
            float3 s4=Source.Load(int3(a4,0)).rgb;

            bool m0=s0.r>0.52 && s0.b>0.52 && (min(s0.r,s0.b)-s0.g)>0.20;
            bool m1=s1.r>0.52 && s1.b>0.52 && (min(s1.r,s1.b)-s1.g)>0.20;
            bool m2=s2.r>0.52 && s2.b>0.52 && (min(s2.r,s2.b)-s2.g)>0.20;
            bool m3=s3.r>0.52 && s3.b>0.52 && (min(s3.r,s3.b)-s3.g)>0.20;
            bool m4=s4.r>0.52 && s4.b>0.52 && (min(s4.r,s4.b)-s4.g)>0.20;

            if(!(m0||m1||m2||m3||m4)) {
                continue; // stale row: do not touch background/text
            }
        }

        //
        // 0.8.0 only checked "bright + neutral".  Because the TMP panel is
        // translucent, a bright road/sky/cab behind the TAB could satisfy the
        // same condition.  While moving the camera this painted a cyan
        // rectangle behind the nickname.
        //
        // 0.8.1 therefore recolours only TEXT-LIKE pixels:
        //   - bright and nearly neutral
        //   - locally brighter than several surrounding pixels
        // Flat/moving panel background is rejected.
        float mx=max(src.r,max(src.g,src.b));
        float mn=min(src.r,min(src.g,src.b));
        float chroma=mx-mn;

        if(mx<0.58 || chroma>0.13) continue;

        int2 p=int2(pos.xy);
        uint tw=0,th=0;
        Source.GetDimensions(tw,th);

        int2 pL=int2(max(p.x-2,0),p.y);
        int2 pR=int2(min(p.x+2,(int)tw-1),p.y);
        int2 pU=int2(p.x,max(p.y-2,0));
        int2 pD=int2(p.x,min(p.y+2,(int)th-1));

        float3 cL=Source.Load(int3(pL,0)).rgb;
        float3 cR=Source.Load(int3(pR,0)).rgb;
        float3 cU=Source.Load(int3(pU,0)).rgb;
        float3 cD=Source.Load(int3(pD,0)).rgb;

        float lum =dot(src.rgb,float3(0.299,0.587,0.114));
        float lL  =dot(cL,float3(0.299,0.587,0.114));
        float lR  =dot(cR,float3(0.299,0.587,0.114));
        float lU  =dot(cU,float3(0.299,0.587,0.114));
        float lD  =dot(cD,float3(0.299,0.587,0.114));

        int darker=0;
        darker += (lum-lL>0.085) ? 1 : 0;
        darker += (lum-lR>0.085) ? 1 : 0;
        darker += (lum-lU>0.085) ? 1 : 0;
        darker += (lum-lD>0.085) ? 1 : 0;

        float localMin=min(min(lL,lR),min(lU,lD));
        float localContrast=lum-localMin;

        // Require a glyph-like local contrast. Very bright text cores get a
        // slightly looser neighbour count so thin letters remain coloured.
        bool textPixel=
            localContrast>0.10 &&
            (darker>=2 || (mx>0.82 && darker>=1));

        if(!textPixel) continue;

        float brightness=saturate((mx-0.55)/0.45);
        float neutral=saturate((0.14-chroma)/0.14);
        float edge=saturate((localContrast-0.08)/0.25);
        float strength=saturate(max(0.55,brightness*neutral)*edge);

        src.rgb=lerp(
            src.rgb,
            items[i].color.rgb,
            strength);

        return src;
    }

    if(!markerPixel) return src;

    // CHAT FIRST-FRAME FALLBACK:
    // Exact chat marker rectangles above always win. This fallback therefore
    // touches only a native magenta shield that appeared/moved before the
    // asynchronous marker detector caught its new position.
    //
    // It is armed only for a very short window around a newly parsed staff
    // message (or a local ENTER submit), so it cannot become the old global
    // "paint every staff with my colour" bug.
    if(viewport.w>0.5 &&
       pos.x<=viewport.w &&
       chatFallback.a>0.5) {

        float strength=saturate((magentaStrength-0.16)/0.84);
        src.rgb=lerp(src.rgb,chatFallback.rgb,strength);
        return src;
    }

    // Anti-flicker fallback:
    // only enabled when exactly one staff color is known in the current panel.
    // If the TAB is dragged before the asynchronous detector catches the new
    // position, the magenta marker is recolored immediately on this frame.
    // viewport.w carries the right edge of the native chat region.
    // NEVER apply the Player Panel anti-flicker fallback inside chat: doing so
    // was what painted a second Game Moderator shield with the local CM color.
    if (viewport.w>0.5 &&
        fallback.a>0.5 &&
        pos.x>viewport.w) {

        float strength=saturate((magentaStrength-0.16)/0.84);
        src.rgb=lerp(src.rgb,fallback.rgb,strength);
    }

    return src;
})";

    ID3DBlob* vsBlob=nullptr;
    ID3DBlob* psBlob=nullptr;
    ID3DBlob* errors=nullptr;

    HRESULT hr=D3DCompile(
        vsSource,
        std::strlen(vsSource),
        nullptr,
        nullptr,
        nullptr,
        "main",
        "vs_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0,
        &vsBlob,
        &errors);

    if (FAILED(hr)) {
        if (errors) Log(std::string("VS compile error: ")+(char*)errors->GetBufferPointer());
        Rel(errors);
        Rel(vsBlob);
        return false;
    }

    Rel(errors);

    hr=D3DCompile(
        psSource,
        std::strlen(psSource),
        nullptr,
        nullptr,
        nullptr,
        "main",
        "ps_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0,
        &psBlob,
        &errors);

    if (FAILED(hr)) {
        if (errors) Log(std::string("PS compile error: ")+(char*)errors->GetBufferPointer());
        Rel(errors);
        Rel(vsBlob);
        Rel(psBlob);
        return false;
    }

    Rel(errors);

    hr=g_device->CreateVertexShader(
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        nullptr,
        &g_vs);

    if (SUCCEEDED(hr)) {
        hr=g_device->CreatePixelShader(
            psBlob->GetBufferPointer(),
            psBlob->GetBufferSize(),
            nullptr,
            &g_ps);
    }

    Rel(vsBlob);
    Rel(psBlob);

    return SUCCEEDED(hr);
}

static bool EnsureFrameCopy(const D3D11_TEXTURE2D_DESC& backDesc) {
    if (g_frameCopy &&
        g_frameW==backDesc.Width &&
        g_frameH==backDesc.Height &&
        g_frameFormat==backDesc.Format) {
        return true;
    }

    Rel(g_frameSrv);
    Rel(g_frameCopy);

    D3D11_TEXTURE2D_DESC d{};
    d.Width=backDesc.Width;
    d.Height=backDesc.Height;
    d.MipLevels=1;
    d.ArraySize=1;
    d.Format=backDesc.Format;
    d.SampleDesc.Count=1;
    d.Usage=D3D11_USAGE_DEFAULT;
    d.BindFlags=D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(g_device->CreateTexture2D(&d,nullptr,&g_frameCopy))) {
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format=backDesc.Format;
    sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MostDetailedMip=0;
    sd.Texture2D.MipLevels=1;

    if (FAILED(g_device->CreateShaderResourceView(
            g_frameCopy,
            &sd,
            &g_frameSrv))) {

        Rel(g_frameCopy);
        return false;
    }

    g_frameW=backDesc.Width;
    g_frameH=backDesc.Height;
    g_frameFormat=backDesc.Format;

    return true;
}

static bool GetBackbuffer(
    ID3D11Texture2D** texture,
    D3D11_TEXTURE2D_DESC& desc) {

    *texture=nullptr;

    ID3D11RenderTargetView* rtv=nullptr;
    g_ctx->OMGetRenderTargets(1,&rtv,nullptr);

    if (!rtv) return false;

    ID3D11Resource* resource=nullptr;
    rtv->GetResource(&resource);
    rtv->Release();

    if (!resource) return false;

    HRESULT hr=resource->QueryInterface(
        __uuidof(ID3D11Texture2D),
        (void**)texture);

    resource->Release();

    if (FAILED(hr) || !*texture) return false;

    (*texture)->GetDesc(&desc);
    return true;
}

static std::vector<PanelStaffEntry> CurrentPanelStaffEntries() {
    RefreshLocalGroupVisibilityFromConfig();

    std::vector<PanelStaffEntry> entries;

    for(const auto& p:g_visiblePlayers) {
        RGB c;
        bool haveColor=false;

        // Prefer TMP's own Team Members Online role mapping.
        // Player ID is first because it survives VTC tags/name formatting.
        if(ColorFromTeamPlayerId(p.playerId,c)) {
            haveColor=true;
        } else {
            auto teamIt=g_teamColorByUsername.find(Lower(p.username));

            if(teamIt!=g_teamColorByUsername.end()) {
                c=teamIt->second;
                haveColor=true;
            } else if(GetStaffColor(p.accountId,c)) {
                haveColor=true;
            }
        }

        if(!haveColor) {
            continue;
        }

        // Local Toggle Group OFF -> do not reserve a marker/color/name entry.
        if(p.local &&
           g_localGroupVisibilityKnown &&
           !g_localGroupVisible) {
            continue;
        }

        PanelStaffEntry e;
        e.color=c;
        e.username=p.username;
        e.playerId=p.playerId;
        e.local=p.local;

        entries.push_back(std::move(e));
    }

    // Current TMP "default" Player Panel order observed in the real client:
    // local player pinned first, then temporary/player ID ascending.
    // g_visiblePlayers is NOT guaranteed to arrive in that visual order.
    std::stable_sort(
        entries.begin(),
        entries.end(),
        [](const PanelStaffEntry& a,const PanelStaffEntry& b) {
            if(a.local!=b.local) return a.local>b.local;
            return a.playerId<b.playerId;
        });

    return entries;
}

static std::vector<RGB> CurrentPanelStaffColors() {
    std::vector<RGB> colors;

    for(const auto& e:CurrentPanelStaffEntries()) {
        colors.push_back(e.color);
    }

    return colors;
}

static bool LocalStaffColor(RGB& out) {
    const uint64_t localAccount=
        g_session->Account().GetAccountID().value_or(0);

    return localAccount && GetStaffColor(localAccount,out);
}

static bool IsLikelyChatMarker(
    const MarkerRect& m,
    UINT width,
    UINT height) {

    (void)height;

    // FIX 0.7.1:
    // The TMP chat is left-anchored, but it is NOT always in the lower half.
    // Expanded chat can place old staff messages near the TOP of the screen.
    //
    // In the current TMP UI the shield inside a chat line sits close to the
    // left edge (after the timestamp).  Use only the horizontal position.
    // The threshold scales a little with resolution but is clamped so a moved
    // Player Panel is not easily mistaken for chat.
    const int chatRight =
        std::clamp((int)(width * 0.12f), 110, 220);

    return m.x < chatRight;
}


static bool CursorInPlayerPanelHeader(
    const POINT& p,
    const std::vector<MarkerRect>& markers,
    UINT width,
    UINT height) {

    (void)width;
    (void)height;

    if(markers.empty()) return false;

    // The first visible staff marker is anchored on a live row.  Build the
    // draggable header zone RELATIVE to that row instead of using the saved
    // config position. This keeps drag detection working after the panel has
    // already been moved somewhere else.
    int minY=markers.front().y;
    int maxMarkerX=markers.front().x;

    for(const auto& m:markers) {
        if(m.y<minY) minY=m.y;
        if(m.x>maxMarkerX) maxMarkerX=m.x;
    }

    int panelWidth=540;
    if(g_playerPanelGeometry.valid && g_playerPanelGeometry.w>200) {
        panelWidth=g_playerPanelGeometry.w;
    }

    const int left=maxMarkerX-panelWidth-40;
    const int right=maxMarkerX+90;

    // Real TMP layout: first row sits roughly 35-110 px below the title/header.
    const int top=minY-125;
    const int bottom=minY-6;

    return
        p.x>=left && p.x<=right &&
        p.y>=top  && p.y<=bottom;
}

static bool GetCursorInBackbuffer(
    UINT width,
    UINT height,
    POINT& out) {

    HWND hwnd=GetForegroundWindow();
    if(!hwnd) return false;

    POINT p{};
    if(!GetCursorPos(&p)) return false;
    if(!ScreenToClient(hwnd,&p)) return false;

    RECT rc{};
    if(!GetClientRect(hwnd,&rc)) return false;

    LONG clientWidthPx=rc.right-rc.left;
    LONG clientHeightPx=rc.bottom-rc.top;

    // RECT fields are LONG on Windows. Avoid std::max(int, LONG), which is
    // ambiguous in MSVC because the two arguments have different types.
    if(clientWidthPx<=0) clientWidthPx=1;
    if(clientHeightPx<=0) clientHeightPx=1;

    out.x=(LONG)std::lround(
        (double)p.x*(double)width/(double)clientWidthPx);

    out.y=(LONG)std::lround(
        (double)p.y*(double)height/(double)clientHeightPx);

    return true;
}

static std::vector<MarkerRect> CurrentDetectedPanelMarkers(
    UINT width,
    UINT height) {

    std::vector<MarkerRect> result;

    for(const auto& m:g_markers) {
        if(IsLikelyChatMarker(m,width,height)) continue;
        result.push_back(m);
    }

    std::sort(
        result.begin(),
        result.end(),
        [](const MarkerRect& a,const MarkerRect& b) {
            if(std::abs(a.y-b.y)>3) return a.y<b.y;
            return a.x<b.x;
        });

    return result;
}

static void UpdatePanelDragFollow(
    UINT width,
    UINT height) {

    RefreshLocalGroupVisibilityFromConfig();

    POINT cursor{};
    const bool haveCursor=
        GetCursorInBackbuffer(width,height,cursor);

    const bool down=
        (GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;

    // Mouse down edge: start following only if the grab began on the TMP panel
    // header and we already have at least one confirmed native staff marker.
    if(down && !g_panelDrag.buttonWasDown && haveCursor) {
        const auto markers=
            CurrentDetectedPanelMarkers(width,height);

        if(CursorInPlayerPanelHeader(cursor,markers,width,height) &&
           !markers.empty()) {

            g_panelDrag.active=true;
            g_panelDrag.releaseHold=false;
            g_panelDrag.startCursor=cursor;
            g_panelDrag.currentCursor=cursor;
            g_panelDrag.dx=0;
            g_panelDrag.dy=0;
            g_panelDrag.baseMarkers=markers;

            Log(
                "TAB DRAG start markers="+
                std::to_string(markers.size())
            );
        }
    }

    if(g_panelDrag.active && down && haveCursor) {
        g_panelDrag.currentCursor=cursor;
        g_panelDrag.dx=
            (int)(cursor.x-g_panelDrag.startCursor.x);
        g_panelDrag.dy=
            (int)(cursor.y-g_panelDrag.startCursor.y);
    }

    // Mouse release: keep the final translated marker positions alive briefly
    // while the async detector catches the panel at its new location.
    if(g_panelDrag.active && !down) {
        g_panelDrag.active=false;
        g_panelDrag.releaseHold=true;
        g_panelDrag.releasedAt=Clock::now();

        Log(
            "TAB DRAG release dx="+
            std::to_string(g_panelDrag.dx)+
            " dy="+std::to_string(g_panelDrag.dy)
        );
    }

    if(g_panelDrag.releaseHold) {
        const auto now=Clock::now();

        // End early when the detector has caught the translated marker set.
        const auto current=
            CurrentDetectedPanelMarkers(width,height);

        bool caughtUp=
            current.size()==g_panelDrag.baseMarkers.size() &&
            !current.empty();

        if(caughtUp) {
            for(size_t i=0;i<current.size();++i) {
                const int expectedX=
                    g_panelDrag.baseMarkers[i].x+
                    g_panelDrag.dx;

                const int expectedY=
                    g_panelDrag.baseMarkers[i].y+
                    g_panelDrag.dy;

                if(std::abs(current[i].x-expectedX)>4 ||
                   std::abs(current[i].y-expectedY)>4) {
                    caughtUp=false;
                    break;
                }
            }
        }

        if(caughtUp ||
           now-g_panelDrag.releasedAt>
               std::chrono::milliseconds(350)) {

            g_panelDrag.releaseHold=false;
            g_panelDrag.baseMarkers.clear();
        }
    }

    g_panelDrag.buttonWasDown=down;
}

static bool PanelDragOverrideActive() {
    return
        (g_panelDrag.active || g_panelDrag.releaseHold) &&
        !g_panelDrag.baseMarkers.empty();
}

static std::vector<MarkerRect> DragTranslatedPanelMarkers() {
    std::vector<MarkerRect> out=g_panelDrag.baseMarkers;

    for(auto& m:out) {
        m.x+=g_panelDrag.dx;
        m.y+=g_panelDrag.dy;
    }

    return out;
}

static bool EnsureRectBuffer(UINT needed) {
    if(needed==0) needed=1;

    if(g_rectBuffer && g_rectSrv && g_rectCapacity>=needed) {
        return true;
    }

    Rel(g_rectSrv);
    Rel(g_rectBuffer);
    g_rectCapacity=0;

    // Grow geometrically, so a busy convoy with many simultaneous staff does
    // not recreate the resource every frame.
    UINT capacity=1;
    while(capacity<needed) {
        if(capacity>16384) {
            capacity=needed;
            break;
        }
        capacity*=2;
    }

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth=capacity*(UINT)sizeof(RectColor);
    bd.Usage=D3D11_USAGE_DYNAMIC;
    bd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride=(UINT)sizeof(RectColor);

    if(FAILED(g_device->CreateBuffer(&bd,nullptr,&g_rectBuffer))) {
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format=DXGI_FORMAT_UNKNOWN;
    sd.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;
    sd.Buffer.FirstElement=0;
    sd.Buffer.NumElements=capacity;

    if(FAILED(g_device->CreateShaderResourceView(
            g_rectBuffer,
            &sd,
            &g_rectSrv))) {

        Rel(g_rectBuffer);
        return false;
    }

    g_rectCapacity=capacity;
    return true;
}

static int FillShaderBuffer(UINT width,UINT height) {
    UpdatePanelDragFollow(width,height);
    UpdateLocalChatSendArm();

    std::vector<MarkerRect> panelMarkers;
    std::vector<MarkerRect> chatMarkers;

    for(const auto& m:g_markers) {
        if(IsLikelyChatMarker(m,width,height)) {
            chatMarkers.push_back(m);
        } else {
            // IMPORTANT 0.8.8:
            // Do NOT compare against config.txt player_panel.position_x/y.
            // TMP does not update that saved rectangle continuously while the
            // panel is being moved, and on this real client it can remain stale
            // after the move.  The detector itself already sees the marker at
            // its new coordinates, so accept every non-chat native staff marker
            // as a Player Panel candidate.
            panelMarkers.push_back(m);
        }
    }

    // ZERO-LAG TAB DRAG PATH:
    // During an actual mouse drag we do NOT wait for the 30 ms GPU->CPU marker
    // readback.  The whole Player Panel moves by exactly the mouse delta, so
    // translate the last confirmed marker set every render frame.
    if(PanelDragOverrideActive()) {
        panelMarkers=DragTranslatedPanelMarkers();
    }

    std::sort(panelMarkers.begin(),panelMarkers.end(),[](const MarkerRect& a,const MarkerRect& b){
        if(std::abs(a.y-b.y)>3) return a.y<b.y;
        return a.x<b.x;
    });

    std::sort(chatMarkers.begin(),chatMarkers.end(),[](const MarkerRect& a,const MarkerRect& b){
        if(std::abs(a.y-b.y)>3) return a.y<b.y;
        return a.x<b.x;
    });

    const auto panelEntries=CurrentPanelStaffEntries();

    // Keep the resolved local role colour warm even across a Toggle Group OFF.
    // Hiding the group must hide the native shield, not forget what colour it
    // should become the instant TMP shows it again.
    RGB warmLocalColor{};
    if(LocalStaffColor(warmLocalColor) && g_cachedLocalShieldValid) {
        g_cachedLocalShieldColor=warmLocalColor;
    }

    std::vector<RGB> panelColors;
    panelColors.reserve(panelEntries.size());
    for(const auto& e:panelEntries) panelColors.push_back(e.color);

    std::vector<RGB> chatColors;
    chatColors.reserve(g_recentChatEvents.size());

    for(const auto& ev:g_recentChatEvents) {
        chatColors.push_back(ev.color);
    }

    const int chatCount=(int)chatMarkers.size();

    if(chatCount!=g_lastChatMarkerDebug ||
       (int)chatColors.size()!=g_lastChatColorDebug) {

        Log(
            "CHAT MAP markers="+std::to_string(chatCount)+
            " roleEvents="+std::to_string(chatColors.size())
        );

        g_lastChatMarkerDebug=chatCount;
        g_lastChatColorDebug=(int)chatColors.size();
    }

    std::vector<RectColor> items;
    items.reserve(panelMarkers.size()*2+chatMarkers.size()+1);

    // Player Panel.
    //
    // FIRST-FRAME /toggle-group protection:
    // always keep the last local shield rectangle armed.  It only reacts to
    // magenta marker pixels, therefore it does nothing while the group is
    // hidden, but recolours the marker immediately when TMP recreates it.
    if(!PanelDragOverrideActive() &&
       g_cachedLocalShieldValid) {
        RectColor cached{};
        cached.rect[0]=(float)g_cachedLocalShield.x;
        cached.rect[1]=(float)g_cachedLocalShield.y;
        cached.rect[2]=(float)(g_cachedLocalShield.x+g_cachedLocalShield.w);
        cached.rect[3]=(float)(g_cachedLocalShield.y+g_cachedLocalShield.h);

        cached.color[0]=g_cachedLocalShieldColor.r/255.0f;
        cached.color[1]=g_cachedLocalShieldColor.g/255.0f;
        cached.color[2]=g_cachedLocalShieldColor.b/255.0f;
        cached.color[3]=1.0f;
        cached.meta[0]=0.0f;

        items.push_back(cached);
    }

    const size_t panelCount=std::min(panelMarkers.size(),panelEntries.size());

    for(size_t i=0;i<panelCount;++i) {
        const auto& m=panelMarkers[i];
        const auto& e=panelEntries[i];
        const auto& c=e.color;

        // Remember the last CONFIRMED local shield position.
        // /toggle-group briefly recreates the native marker in magenta before
        // the asynchronous detector/config parser catches up.  Keeping this
        // tiny spatial cache lets the pixel shader recolour that first frame.
        if(e.local) {
            g_cachedLocalShield=m;
            g_cachedLocalShieldColor=c;
            g_cachedLocalShieldValid=true;
        }

        // 1) Native shield.
        RectColor shield{};
        shield.rect[0]=(float)std::max(0,m.x-4);
        shield.rect[1]=(float)std::max(0,m.y-4);
        shield.rect[2]=(float)std::min((int)width,m.x+m.w+4);
        shield.rect[3]=(float)std::min((int)height,m.y+m.h+4);

        shield.color[0]=c.r/255.0f;
        shield.color[1]=c.g/255.0f;
        shield.color[2]=c.b/255.0f;
        shield.color[3]=1.0f;
        shield.meta[0]=0.0f;

        items.push_back(shield);

        // 2) TAB nickname immediately to the LEFT of the native shield.
        //
        // TMP's 100% HUD font is ~7 px per username character in the Player
        // Panel. The extra padding covers narrow/wide glyph differences.
        // The rectangle is clamped so we do NOT reach the ID/speaker columns.
        if(!e.username.empty()) {
            const int nicknameRight=std::max(0,m.x-2);

            int estimatedWidth=
                (int)std::lround((double)e.username.size()*7.05)+6;

            estimatedWidth=std::clamp(estimatedWidth,32,210);

            const int nicknameLeft=
                std::max(0,nicknameRight-estimatedWidth);

            RectColor nick{};
            nick.rect[0]=(float)nicknameLeft;
            nick.rect[1]=(float)std::max(0,m.y-2);
            nick.rect[2]=(float)nicknameRight;
            nick.rect[3]=(float)std::min((int)height,m.y+m.h+2);

            nick.color[0]=c.r/255.0f;
            nick.color[1]=c.g/255.0f;
            nick.color[2]=c.b/255.0f;
            nick.color[3]=1.0f;
            nick.meta[0]=PanelDragOverrideActive() ? 2.0f : 1.0f;
            nick.meta[1]=(float)(m.x+m.w/2);
            nick.meta[2]=(float)(m.y+m.h/2);
            nick.meta[3]=(float)std::max(2,std::min(m.w,m.h)/4);

            items.push_back(nick);
        }
    }

    // Chat.
    //
    // There is NO fixed number of role events anymore.  If 2, 20 or 100 staff
    // lines are simultaneously visible/known, the buffer grows dynamically.
    if(chatCount>0) {
        if((int)chatColors.size()>=chatCount) {
            const int colorStart=(int)chatColors.size()-chatCount;

            for(int i=0;i<chatCount;++i) {
                const auto& m=chatMarkers[(size_t)i];
                const auto& c=chatColors[(size_t)(colorStart+i)];

                RectColor rc{};
                rc.rect[0]=(float)std::max(0,m.x-4);
                rc.rect[1]=(float)std::max(0,m.y-4);
                rc.rect[2]=(float)std::min((int)width,m.x+m.w+4);
                rc.rect[3]=(float)std::min((int)height,m.y+m.h+4);

                rc.color[0]=c.r/255.0f;
                rc.color[1]=c.g/255.0f;
                rc.color[2]=c.b/255.0f;
                rc.color[3]=1.0f;
                rc.meta[0]=0.0f;

                items.push_back(rc);
            }
        } else if(chatCount==1 && chatColors.empty()) {
            RGB localColor;
            if(LocalStaffColor(localColor)) {
                const auto& m=chatMarkers.front();

                RectColor rc{};
                rc.rect[0]=(float)std::max(0,m.x-4);
                rc.rect[1]=(float)std::max(0,m.y-4);
                rc.rect[2]=(float)std::min((int)width,m.x+m.w+4);
                rc.rect[3]=(float)std::min((int)height,m.y+m.h+4);

                rc.color[0]=localColor.r/255.0f;
                rc.color[1]=localColor.g/255.0f;
                rc.color[2]=localColor.b/255.0f;
                rc.color[3]=1.0f;
                rc.meta[0]=0.0f;

                items.push_back(rc);
            }
        }
    }

    if(!EnsureRectBuffer((UINT)items.size())) {
        return 0;
    }

    if(!items.empty()) {
        D3D11_MAPPED_SUBRESOURCE rectMapped{};

        if(FAILED(g_ctx->Map(
                g_rectBuffer,
                0,
                D3D11_MAP_WRITE_DISCARD,
                0,
                &rectMapped))) {
            return 0;
        }

        std::memcpy(
            rectMapped.pData,
            items.data(),
            items.size()*sizeof(RectColor));

        g_ctx->Unmap(g_rectBuffer,0);
    }

    D3D11_MAPPED_SUBRESOURCE cbMapped{};

    if(FAILED(g_ctx->Map(
            g_cb,
            0,
            D3D11_MAP_WRITE_DISCARD,
            0,
            &cbMapped))) {
        return 0;
    }

    auto* cb=(ShaderCB*)cbMapped.pData;
    std::memset(cb,0,sizeof(ShaderCB));

    cb->viewport[0]=(float)width;
    cb->viewport[1]=(float)height;
    cb->viewport[2]=(float)items.size();

    // Anti-flicker fallback is safe ONLY when there is exactly one native
    // staff shield in the LIVE Player Panel and exactly one resolved staff row.
    //
    // Previous builds enabled it whenever only one colour was RESOLVED. If a
    // second staff shield existed but its role was not resolved yet, every
    // magenta marker outside chat could temporarily/permanently inherit the
    // local Community Moderator cyan. That is exactly what happened to
    // Valhalla.
    if(panelEntries.size()==1) {
        // With exactly one resolved Player Panel staff member, every native
        // magenta shield outside the chat can safely use that colour.  This
        // gives the shield a same-frame fallback while dragging, even before
        // the asynchronous detector publishes its next position.
        const RGB c=panelEntries.front().color;
        const float chatRight=(float)std::clamp((int)(width*0.12f),110,220);

        cb->viewport[3]=chatRight;
        cb->fallback[0]=c.r/255.0f;
        cb->fallback[1]=c.g/255.0f;
        cb->fallback[2]=c.b/255.0f;
        cb->fallback[3]=1.0f;
    }

    // Short-lived chat anti-flash colour.
    //
    // Priority:
    // 1) newest parsed staff chat event, if it is extremely recent;
    // 2) local staff colour for the ENTER-submit arm window, which covers the
    //    frame before TMP has flushed the local chat line to disk.
    const auto chatNow=Clock::now();
    bool chatFallbackSet=false;

    if(!g_recentChatEvents.empty()) {
        const auto& newest=g_recentChatEvents.back();

        if(chatNow-newest.when<std::chrono::milliseconds(180)) {
            cb->chatFallback[0]=newest.color.r/255.0f;
            cb->chatFallback[1]=newest.color.g/255.0f;
            cb->chatFallback[2]=newest.color.b/255.0f;
            cb->chatFallback[3]=1.0f;
            chatFallbackSet=true;
        }
    }

    if(!chatFallbackSet &&
       chatNow<g_localChatFallbackUntil) {

        RGB local{};

        if(LocalStaffColor(local)) {
            cb->chatFallback[0]=local.r/255.0f;
            cb->chatFallback[1]=local.g/255.0f;
            cb->chatFallback[2]=local.b/255.0f;
            cb->chatFallback[3]=1.0f;
        }
    }

    g_ctx->Unmap(g_cb,0);

    return (int)items.size();
}

static void DrawRecolorPass(
    ID3D11Texture2D* backbuffer,
    const D3D11_TEXTURE2D_DESC& desc) {

    const int activeCount=FillShaderBuffer(desc.Width,desc.Height);

    // Fallback may be active even while marker rect list is momentarily empty.
    if (activeCount<=0) {
        const auto panelColors=CurrentPanelStaffColors();
        if (panelColors.size()!=1) return;
    }

    if (!EnsureFrameCopy(desc)) return;

    if (desc.SampleDesc.Count>1) {
        g_ctx->ResolveSubresource(
            g_frameCopy,
            0,
            backbuffer,
            0,
            desc.Format);
    } else {
        g_ctx->CopyResource(
            g_frameCopy,
            backbuffer);
    }

    ID3D11InputLayout* oldLayout=nullptr;
    D3D11_PRIMITIVE_TOPOLOGY oldTopology{};
    ID3D11VertexShader* oldVS=nullptr;
    ID3D11PixelShader* oldPS=nullptr;
    ID3D11Buffer* oldVSCB=nullptr;
    ID3D11Buffer* oldPSCB=nullptr;
    ID3D11ShaderResourceView* oldSrv=nullptr;
    ID3D11ShaderResourceView* oldRectSrv=nullptr;
    ID3D11BlendState* oldBlend=nullptr;
    ID3D11DepthStencilState* oldDepth=nullptr;
    ID3D11RasterizerState* oldRaster=nullptr;
    ID3D11RenderTargetView* oldRTV=nullptr;
    ID3D11DepthStencilView* oldDSV=nullptr;

    D3D11_VIEWPORT oldViewports[
        D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    UINT oldViewportCount=
        D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;

    FLOAT oldBlendFactor[4]{};
    UINT oldSampleMask=0;
    UINT oldStencilRef=0;

    g_ctx->IAGetInputLayout(&oldLayout);
    g_ctx->IAGetPrimitiveTopology(&oldTopology);
    g_ctx->VSGetShader(&oldVS,nullptr,nullptr);
    g_ctx->PSGetShader(&oldPS,nullptr,nullptr);
    g_ctx->VSGetConstantBuffers(0,1,&oldVSCB);
    g_ctx->PSGetConstantBuffers(0,1,&oldPSCB);
    g_ctx->PSGetShaderResources(0,1,&oldSrv);
    g_ctx->PSGetShaderResources(1,1,&oldRectSrv);
    g_ctx->OMGetBlendState(&oldBlend,oldBlendFactor,&oldSampleMask);
    g_ctx->OMGetDepthStencilState(&oldDepth,&oldStencilRef);
    g_ctx->RSGetState(&oldRaster);
    g_ctx->OMGetRenderTargets(1,&oldRTV,&oldDSV);
    g_ctx->RSGetViewports(&oldViewportCount,oldViewports);

    // IMPORTANT:
    // OnPostRender does not guarantee that TMP has left the swap-chain
    // backbuffer or a full-screen viewport bound. While dragging/moving UI
    // windows TMP can change those states. Create/bind an RTV for the exact
    // backbuffer we copied above and force a full-backbuffer viewport.
    ID3D11RenderTargetView* backbufferRTV=nullptr;

    if(FAILED(g_device->CreateRenderTargetView(
            backbuffer,
            nullptr,
            &backbufferRTV))) {

        Rel(oldDSV);
        Rel(oldRTV);
        Rel(oldRaster);
        Rel(oldDepth);
        Rel(oldBlend);
        Rel(oldRectSrv);
        Rel(oldSrv);
        Rel(oldPSCB);
        Rel(oldVSCB);
        Rel(oldPS);
        Rel(oldVS);
        Rel(oldLayout);

        return;
    }

    FLOAT blendFactor[4]={0,0,0,0};

    g_ctx->IASetInputLayout(nullptr);
    g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    g_ctx->VSSetShader(g_vs,nullptr,0);
    g_ctx->VSSetConstantBuffers(0,1,&g_cb);

    g_ctx->PSSetShader(g_ps,nullptr,0);
    g_ctx->PSSetConstantBuffers(0,1,&g_cb);
    g_ctx->PSSetShaderResources(0,1,&g_frameSrv);
    g_ctx->PSSetShaderResources(1,1,&g_rectSrv);

    g_ctx->OMSetRenderTargets(1,&backbufferRTV,nullptr);

    D3D11_VIEWPORT fullViewport{};
    fullViewport.TopLeftX=0.0f;
    fullViewport.TopLeftY=0.0f;
    fullViewport.Width=(FLOAT)desc.Width;
    fullViewport.Height=(FLOAT)desc.Height;
    fullViewport.MinDepth=0.0f;
    fullViewport.MaxDepth=1.0f;
    g_ctx->RSSetViewports(1,&fullViewport);

    g_ctx->OMSetBlendState(g_blend,blendFactor,0xffffffff);
    g_ctx->OMSetDepthStencilState(g_depth,0);
    g_ctx->RSSetState(g_raster);

    g_ctx->Draw(3,0);

    ID3D11ShaderResourceView* nullSrvs[2]={nullptr,nullptr};
    g_ctx->PSSetShaderResources(0,2,nullSrvs);

    g_ctx->IASetInputLayout(oldLayout);
    g_ctx->IASetPrimitiveTopology(oldTopology);
    g_ctx->VSSetShader(oldVS,nullptr,0);
    g_ctx->VSSetConstantBuffers(0,1,&oldVSCB);
    g_ctx->PSSetShader(oldPS,nullptr,0);
    g_ctx->PSSetConstantBuffers(0,1,&oldPSCB);
    g_ctx->PSSetShaderResources(0,1,&oldSrv);
    g_ctx->PSSetShaderResources(1,1,&oldRectSrv);

    // Put TMP's render pipeline back exactly as we found it.
    g_ctx->OMSetRenderTargets(1,&oldRTV,oldDSV);

    if(oldViewportCount>0) {
        g_ctx->RSSetViewports(oldViewportCount,oldViewports);
    }

    g_ctx->OMSetBlendState(oldBlend,oldBlendFactor,oldSampleMask);
    g_ctx->OMSetDepthStencilState(oldDepth,oldStencilRef);
    g_ctx->RSSetState(oldRaster);

    Rel(backbufferRTV);
    Rel(oldDSV);
    Rel(oldRTV);
    Rel(oldRaster);
    Rel(oldDepth);
    Rel(oldBlend);
    Rel(oldRectSrv);
    Rel(oldSrv);
    Rel(oldPSCB);
    Rel(oldVSCB);
    Rel(oldPS);
    Rel(oldVS);
    Rel(oldLayout);
}

static bool InitD3D() {
    auto renderer=g_session->Render().GetRendererID();

    if (!renderer ||
        *renderer!=TruckersMP::RendererID::DirectX11) {
        Log("Renderer is not DirectX11");
        return false;
    }

    auto handle=g_session->Render().GetDeviceHandle();

    if (!handle || !*handle) {
        Log("No D3D11 device handle");
        return false;
    }

    g_device=
        reinterpret_cast<ID3D11Device*>(
            static_cast<uintptr_t>(*handle));

    g_device->AddRef();
    g_device->GetImmediateContext(&g_ctx);

    if (!g_ctx) return false;

    if (!CompileShaders()) return false;

    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.ByteWidth=sizeof(ShaderCB);
    cbDesc.Usage=D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;

    if (FAILED(g_device->CreateBuffer(&cbDesc,nullptr,&g_cb))) {
        return false;
    }

    D3D11_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].BlendEnable=FALSE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask=
        D3D11_COLOR_WRITE_ENABLE_ALL;

    if (FAILED(g_device->CreateBlendState(&blendDesc,&g_blend))) {
        return false;
    }

    D3D11_DEPTH_STENCIL_DESC depthDesc{};
    depthDesc.DepthEnable=FALSE;
    depthDesc.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
    depthDesc.StencilEnable=FALSE;

    if (FAILED(g_device->CreateDepthStencilState(&depthDesc,&g_depth))) {
        return false;
    }

    D3D11_RASTERIZER_DESC rasterDesc{};
    rasterDesc.FillMode=D3D11_FILL_SOLID;
    rasterDesc.CullMode=D3D11_CULL_NONE;
    rasterDesc.DepthClipEnable=TRUE;
    rasterDesc.ScissorEnable=FALSE;

    if (FAILED(g_device->CreateRasterizerState(&rasterDesc,&g_raster))) {
        return false;
    }

    return true;
}

// -----------------------------------------------------------------------------
// Per-frame
// -----------------------------------------------------------------------------

static void OnPostRender() {
    RefreshVisiblePlayers();
    PollChatLog();

    ID3D11Texture2D* backbuffer=nullptr;
    D3D11_TEXTURE2D_DESC desc{};

    if (!GetBackbuffer(&backbuffer,desc)) return;

    if (!IsSupportedFormat(desc.Format)) {
        backbuffer->Release();
        return;
    }

    ProcessReadyReadbacks();
    SubmitReadback(backbuffer,desc);
    DrawRecolorPass(backbuffer,desc);

    backbuffer->Release();
}


static std::string TrimConfig(std::string s) {
    while(!s.empty() &&
          std::isspace((unsigned char)s.front())) {
        s.erase(s.begin());
    }

    while(!s.empty() &&
          std::isspace((unsigned char)s.back())) {
        s.pop_back();
    }

    return s;
}

static void LoadBetterGroupConfig() {
    std::ifstream f(
        PluginDir() + "\\BetterGroup.cfg");

    if(!f) {
        g_debug=false;
        return;
    }

    std::string line;

    while(std::getline(f,line)) {
        line=TrimConfig(line);

        if(line.empty() ||
           line[0]=='#' ||
           line[0]==';') {
            continue;
        }

        const auto eq=line.find('=');
        if(eq==std::string::npos) continue;

        const std::string key=
            Lower(TrimConfig(line.substr(0,eq)));

        const std::string value=
            Lower(TrimConfig(line.substr(eq+1)));

        if(key=="debug") {
            g_debug=
                value=="1" ||
                value=="true" ||
                value=="yes" ||
                value=="on";
        }
    }
}

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------

TMP_EXPORT bool TMP_API truckersmp_init(
    const TruckersMP_Host* host,
    TruckersMP_PluginDesc* desc) {

    using namespace TruckersMP;

    PluginInfo info;
    info.m_name="BetterGroup";
    info.m_author="Dade";
    info.m_version="1.0.0-rc3.2";
    info.m_description=
        "Role-aware TruckersMP group styling for Player Panel and chat.";

    FillPluginDesc(desc,info);

    LoadBetterGroupConfig();

    g_session=Session::Create(host);
    if (!g_session) return false;

    if(g_debug) {
        std::ofstream clear(
            PluginDir()+"\\BetterGroup.log",
            std::ios::trunc);
    } else {
        DeleteFileA(
            (PluginDir()+"\\BetterGroup.log").c_str());
    }

    LoadRoles();

    if (!InitD3D()) {
        Log("DirectX init FAILED");

        g_session->UserInterface().ShowNotification(
            NotificationType::Error,
            "BetterGroup 1.0.0 RC3.2: DirectX init failed");

        return true;
    }

    g_stop=false;
    g_apiThread=std::thread(ApiWorker);

    g_session->Render().OnPostRender.Register(OnPostRender);

    Log(
        "BetterGroup 1.0.0-rc3.2 loaded - public pack");

    return true;
}

TMP_EXPORT void TMP_API truckersmp_shutdown(void) {
    g_stop=true;
    g_apiCv.notify_all();

    if (g_apiThread.joinable()) {
        g_apiThread.join();
    }

    for (auto& slot:g_slots) {
        DestroySlot(slot);
    }

    Rel(g_raster);
    Rel(g_depth);
    Rel(g_blend);
    Rel(g_rectSrv);
    Rel(g_rectBuffer);
    Rel(g_cb);
    Rel(g_ps);
    Rel(g_vs);

    Rel(g_frameSrv);
    Rel(g_frameCopy);

    Rel(g_ctx);
    Rel(g_device);

    g_session.reset();
}

BOOL APIENTRY DllMain(
    HMODULE module,
    DWORD reason,
    LPVOID) {

    if (reason==DLL_PROCESS_ATTACH) {
        g_module=module;
        DisableThreadLibraryCalls(module);
    }

    return TRUE;
}
