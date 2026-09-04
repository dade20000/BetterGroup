if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "GenerateRc33.cmake requires -DINPUT=... -DOUTPUT=...")
endif()

file(READ "${INPUT}" SOURCE)

macro(REPLACE_REQUIRED LABEL)
  string(FIND "${SOURCE}" "${OLD}" FOUND_AT)
  if(FOUND_AT EQUAL -1)
    message(FATAL_ERROR "RC3.3 source transform failed at: ${LABEL}")
  endif()
  string(REPLACE "${OLD}" "${NEW}" SOURCE "${SOURCE}")
endmacro()

# Version / build identity.
set(OLD "1.0.0-rc3.2")
set(NEW "1.0.0-rc3.3")
REPLACE_REQUIRED("version strings")

set(OLD "RC3.2")
set(NEW "RC3.3")
REPLACE_REQUIRED("display version strings")

set(OLD "loaded - public pack")
set(NEW "loaded - chat native-color-aware names")
REPLACE_REQUIRED("debug build label")

# Keep username with each parsed staff chat event so the exact nickname width
# can be targeted without touching role/ID/message text.
set(OLD [=[struct ChatStaffEvent {
    RGB color;
    int playerId=-1;
    Clock::time_point when{};
};]=])
set(NEW [=[struct ChatStaffEvent {
    RGB color;
    int playerId=-1;
    std::string username;
    Clock::time_point when{};
};]=])
REPLACE_REQUIRED("ChatStaffEvent username")

set(OLD "static void PushChatEvent(const RGB& c,int playerId) {")
set(NEW "static void PushChatEvent(const RGB& c,int playerId,const std::string& username) {")
REPLACE_REQUIRED("PushChatEvent signature")

set(OLD [=[        if(last.playerId==playerId &&
           last.color==c &&
           now-last.when<std::chrono::milliseconds(80)) {]=])
set(NEW [=[        if(last.playerId==playerId &&
           last.color==c &&
           Lower(last.username)==Lower(username) &&
           now-last.when<std::chrono::milliseconds(80)) {]=])
REPLACE_REQUIRED("chat event dedupe")

set(OLD "g_recentChatEvents.push_back({c,playerId,now});")
set(NEW "g_recentChatEvents.push_back({c,playerId,username,now});")
REPLACE_REQUIRED("chat event aggregate")

set(OLD "PushChatEvent(c,playerId);")
set(NEW "PushChatEvent(c,playerId,username);")
REPLACE_REQUIRED("PushChatEvent callers")

# Shader mode 3: chat nickname. It reuses the existing neutral/text-like mask,
# so native chromatic role names are left untouched.
set(OLD [=[        // mode 1 = TAB nickname with shield-anchor guard.
        // mode 2 = TAB nickname while the panel is actively being dragged.]=])
set(NEW [=[        // mode 1 = TAB nickname with shield-anchor guard.
        // mode 2 = TAB nickname while the panel is actively being dragged.
        // mode 3 = CHAT nickname, but ONLY while TMP rendered it in its
        //          default neutral/white style. Native coloured staff names
        //          (Event Team, Media Team, Game Producer, etc.) are preserved.]=])
REPLACE_REQUIRED("shader mode 3 comment")

set(OLD "        if(items[i].meta.x<1.5) {")
set(NEW [=[        bool needsShieldAnchor =
            (items[i].meta.x>=0.5 && items[i].meta.x<1.5) ||
            (items[i].meta.x>=2.5 && items[i].meta.x<3.5);

        if(needsShieldAnchor) {]=])
REPLACE_REQUIRED("mode 3 shield anchor guard")

set(OLD [=[        float mx=max(src.r,max(src.g,src.b));
        float mn=min(src.r,min(src.g,src.b));
        float chroma=mx-mn;

        if(mx<0.58 || chroma>0.13) continue;]=])
set(NEW [=[        float mx=max(src.r,max(src.g,src.b));
        float mn=min(src.r,min(src.g,src.b));
        float chroma=mx-mn;

        // Native-colour protection for chat names (mode 3). If TruckersMP
        // already renders a staff nickname in a real role colour, chroma is
        // non-neutral and we leave it untouched. White/default nicknames pass
        // through and BetterGroup applies the resolved role colour.
        if(mx<0.58 || chroma>0.13) continue;]=])
REPLACE_REQUIRED("native colour guard comment")

# Keep username and colour arrays aligned with the recent event queue.
set(OLD [=[    std::vector<RGB> chatColors;
    chatColors.reserve(g_recentChatEvents.size());

    for(const auto& ev:g_recentChatEvents) {
        chatColors.push_back(ev.color);
    }]=])
set(NEW [=[    std::vector<RGB> chatColors;
    std::vector<std::string> chatUsernames;
    chatColors.reserve(g_recentChatEvents.size());
    chatUsernames.reserve(g_recentChatEvents.size());

    for(const auto& ev:g_recentChatEvents) {
        chatColors.push_back(ev.color);
        chatUsernames.push_back(ev.username);
    }]=])
REPLACE_REQUIRED("chat username array")

set(OLD "items.reserve(panelMarkers.size()*2+chatMarkers.size()+1);")
set(NEW "items.reserve(panelMarkers.size()*2+chatMarkers.size()*2+1);")
REPLACE_REQUIRED("rect buffer reserve")

# Add a nickname rectangle immediately to the right of each mapped chat shield.
# Only mode 3 text-like neutral pixels are recoloured. Existing TMP role colours
# remain untouched because chromatic pixels fail the neutral mask.
set(OLD [=[                rc.meta[0]=0.0f;

                items.push_back(rc);
            }
        } else if(chatCount==1 && chatColors.empty()) {]=])
set(NEW [=[                rc.meta[0]=0.0f;

                items.push_back(rc);

                // CHAT nickname is immediately to the RIGHT of the staff
                // shield in TMP's native chat row. Only the username width is
                // covered so we do not recolour role/ID/message text.
                //
                // Shader mode 3 is neutral-only: if TMP already gave the
                // nickname a native staff colour on an Event/Simulation
                // server, BetterGroup leaves those chromatic pixels exactly
                // as TMP rendered them.
                const auto& username=chatUsernames[(size_t)(colorStart+i)];
                if(!username.empty()) {
                    const int nicknameLeft=
                        std::min((int)width,std::max(0,m.x+m.w+3));

                    int estimatedWidth=
                        (int)std::lround((double)username.size()*7.15)+8;
                    estimatedWidth=std::clamp(estimatedWidth,28,240);

                    const int nicknameRight=
                        std::min((int)width,nicknameLeft+estimatedWidth);

                    if(nicknameRight>nicknameLeft) {
                        RectColor nick{};
                        nick.rect[0]=(float)nicknameLeft;
                        nick.rect[1]=(float)std::max(0,m.y-2);
                        nick.rect[2]=(float)nicknameRight;
                        nick.rect[3]=(float)std::min((int)height,m.y+m.h+2);

                        nick.color[0]=c.r/255.0f;
                        nick.color[1]=c.g/255.0f;
                        nick.color[2]=c.b/255.0f;
                        nick.color[3]=1.0f;
                        nick.meta[0]=3.0f;
                        nick.meta[1]=(float)(m.x+m.w/2);
                        nick.meta[2]=(float)(m.y+m.h/2);
                        nick.meta[3]=(float)std::max(2,std::min(m.w,m.h)/4);

                        items.push_back(nick);
                    }
                }
            }
        } else if(chatCount==1 && chatColors.empty()) {]=])
REPLACE_REQUIRED("chat nickname rectangles")

get_filename_component(OUTPUT_DIR "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(WRITE "${OUTPUT}" "${SOURCE}")
