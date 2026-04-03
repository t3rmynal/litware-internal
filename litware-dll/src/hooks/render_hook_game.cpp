// игра

static void BuildESPData(){
    g_esp_count=0;g_esp_oof_count=0;g_esp_local_team=0;g_esp_local_pawn=0;
    g_localOrigin = {};
    static int s_visFrame = 0;
    if((s_visFrame % 3) == 0) std::fill(std::begin(g_visMap), std::end(g_visMap), false);
    s_visFrame++;

    EnsureModules();if(!g_client)return;
    uintptr_t entityList=Rd<uintptr_t>(g_client+offsets::client::dwEntityList);
    if(!entityList)return;
    const float*vm=reinterpret_cast<const float*>(g_client+offsets::client::dwViewMatrix);
    int sw=1920, sh=1080;
    if(g_bbWidth >= 100 && g_bbHeight >= 100){
        sw = (int)g_bbWidth;
        sh = (int)g_bbHeight;
    }else if(g_engine2){
        int w = Rd<int>(g_engine2+offsets::engine2::dwWindowWidth);
        int h = Rd<int>(g_engine2+offsets::engine2::dwWindowHeight);
        if(w > 100 && h > 100){ sw = w; sh = h; }
    }
    uintptr_t localPawn=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);
    (void)Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerController);
    int localTeam=0;Vec3 localOrigin{};
    if(localPawn){localTeam=(int)Rd<uint8_t>(localPawn+offsets::base_entity::m_iTeamNum);
    uintptr_t sc0=Rd<uintptr_t>(localPawn+offsets::base_entity::m_pGameSceneNode);
    if(sc0)localOrigin=Rd<Vec3>(sc0+offsets::scene_node::m_vecAbsOrigin);}
    g_localOrigin = localOrigin;
    g_esp_local_team=localTeam;g_esp_local_pawn=localPawn;
    g_esp_screen_w=sw;g_esp_screen_h=sh;
    for(int i=1;i<64&&g_esp_count<ESP_MAX_PLAYERS;i++){
        uintptr_t chunk=Rd<uintptr_t>(entityList+8*((i&0x7FFF)>>9)+16);if(!chunk)continue;
        uintptr_t ctrl=Rd<uintptr_t>(chunk+112*(i&0x1FF));if(!ctrl)continue;
        if(!Rd<bool>(ctrl+offsets::controller::m_bPawnIsAlive))continue;
        uint32_t ph=Rd<uint32_t>(ctrl+offsets::controller::m_hPlayerPawn);if(!ph)continue;
        uintptr_t pchunk=Rd<uintptr_t>(entityList+8*((ph&0x7FFF)>>9)+16);if(!pchunk)continue;
        uintptr_t pawn=Rd<uintptr_t>(pchunk+112*(ph&0x1FF));
        if(!pawn||pawn==localPawn)continue;
        int team=(int)Rd<uint8_t>(pawn+offsets::base_entity::m_iTeamNum);
        int health=Rd<int>(pawn+offsets::base_entity::m_iHealth);if(health<=0)continue;
        bool vis=true;
        if(g_visCheckEnabled && i > 0 && i <= ESP_MAX_PLAYERS){
            if((s_visFrame % 3) == 0){
                vis=Rd<bool>(pawn+offsets::spotted::m_entitySpottedState+offsets::spotted::m_bSpotted);
                g_visMap[i] = vis;
            }else{
                vis = g_visMap[i];
            }
        }
        if(i > 0 && i <= ESP_MAX_PLAYERS && vis) g_visLastTrueTick[i] = GetTickCount64();
        UINT64 now = GetTickCount64();
        bool effVis = vis || (i > 0 && i <= ESP_MAX_PLAYERS && (now - g_visLastTrueTick[i]) < ESP_STALE_MS);
        if(g_espOnlyVis && !effVis) continue;
        uintptr_t scn=Rd<uintptr_t>(pawn+offsets::base_entity::m_pGameSceneNode);
        Vec3 origin{};if(scn)origin=Rd<Vec3>(scn+offsets::scene_node::m_vecAbsOrigin);
        Vec3 viewOff=Rd<Vec3>(pawn+offsets::base_pawn::m_vecViewOffset);Vec3 head=origin+viewOff;
        Vec3 headForward=head;
        float yaw=0.f;
        if(scn){
            yaw=Rd<float>(scn+offsets::scene_node::m_angRotation+4);
            float rad=yaw*(3.14159265f/180.f);
            Vec3 fwd{cosf(rad),sinf(rad),0.f};
            headForward = head + fwd * g_espHeadForward;
        }
        float hx,hy,fx,fy,hfx,hfy;
        if(!WorldToScreen(vm,head,sw,sh,hx,hy)){
            if(g_espOof&&(team!=localTeam)&&g_esp_oof_count<32){
                float ox,oy;
                if(GetOofArrowPos(vm,head,sw,sh,ox,oy)){
                    float cx=sw*0.5f, cy=sh*0.5f;
                    float dx=ox-cx, dy=oy-cy;
                    float angle=atan2f(dx, -dy);
                    float*ecol=(team==localTeam)?g_espTeamCol:g_espEnemyCol;
                    g_esp_oof[g_esp_oof_count].x=ox;
                    g_esp_oof[g_esp_oof_count].y=oy;
                    g_esp_oof[g_esp_oof_count].angle=angle;
                    g_esp_oof[g_esp_oof_count].col=IM_COL32((int)(ecol[0]*255),(int)(ecol[1]*255),(int)(ecol[2]*255),220);
                    g_esp_oof_count++;
                }
            }
            continue;
        }
        if(!WorldToScreen(vm,headForward,sw,sh,hfx,hfy)){hfx=hx;hfy=hy;}
        if(!WorldToScreen(vm,origin,sw,sh,fx,fy))continue;
        float boxH = fy - hy;
        float top = hy - boxH * 0.18f;
        boxH = fy - top;
        float boxW = boxH * 0.52f;
        float cx = (hx + fx) * 0.5f;
        float dist = (origin - localOrigin).length() / 100.f;
        uintptr_t namePtr=Rd<uintptr_t>(ctrl+offsets::controller::m_sSanitizedPlayerName);
        float flashDur=Rd<float>(pawn+offsets::cs_pawn_base::m_flFlashDuration);
        ESPEntry&e=g_esp_players[g_esp_count++];
        e.valid=true;e.visible=effVis;e.flashed=(flashDur>0.1f);
        e.planting=Rd<bool>(pawn+offsets::cs_pawn::m_bIsPlantingViaUse);
        e.scoped=Rd<bool>(pawn+offsets::cs_pawn::m_bIsScoped);
        e.box_l = cx - boxW * 0.5f;
        e.box_t = top;
        e.box_r = cx + boxW * 0.5f;
        e.box_b = fy;
        e.defusing=(g_bombDefusing&&g_bombDefuserPawn==pawn);
        e.hasBomb=entityList?PlayerHasWeaponId(pawn,entityList,88):false;
        e.hasKits=Rd<bool>(ctrl+offsets::controller::m_bPawnHasDefuser);
        e.spotted=effVis;
        e.ent_index=i;
        e.pawn=pawn;
        e.controller=ctrl;
        e.head_x=hx;e.head_y=hy;
        e.chest_x=(hx+fx)*0.5f;e.chest_y=(hy+fy)*0.5f;
        e.head_fx=hfx;e.head_fy=hfy;
        e.head_ox=head.x;e.head_oy=head.y;e.head_oz=head.z;
        e.origin_x=origin.x;e.origin_y=origin.y;e.origin_z=origin.z;
        e.feet_x=fx;e.feet_y=fy;
        e.health=health;e.team=team;e.distance=dist;e.yaw=0.f;
        RdName(namePtr,e.name,sizeof(e.name));
        g_esp_stale[i] = e;
        g_esp_stale_tick[i] = GetTickCount64();
    }
}

static void ReadPlayerName(uintptr_t controller, char* buf, size_t maxlen){
    if(!buf || !maxlen){ return; }
    buf[0] = '\0';
    if(!controller) return;

    uintptr_t nameAddr = controller + offsets::controller::m_sSanitizedPlayerName;
    RdName(nameAddr, buf, maxlen);
    if(buf[0]) return;

    uintptr_t namePtr = Rd<uintptr_t>(nameAddr);
    if(namePtr) RdName(namePtr, buf, maxlen);
}

static char g_spectatorNames[16][64];
static int g_spectatorCount = 0;
static char g_spectatingTarget[64] = {};
static bool g_weAreSpectating = false;

static void BuildSpectatorList(){
    constexpr int kMaxSpectators = (int)(sizeof(g_spectatorNames) / sizeof(g_spectatorNames[0]));
    g_spectatorCount = 0;
    g_spectatingTarget[0] = '\0';
    g_weAreSpectating = false;
    if(!g_client||!g_spectatorListEnabled) return;
    uintptr_t entityList = Rd<uintptr_t>(g_client + offsets::client::dwEntityList);
    if(!entityList) return;
    uintptr_t localPawn = Rd<uintptr_t>(g_client + offsets::client::dwLocalPlayerPawn);
    if(!localPawn) return;
    int localLife = Rd<uint8_t>(localPawn + offsets::base_entity::m_lifeState);
    uintptr_t localCtrl = Rd<uintptr_t>(g_client + offsets::client::dwLocalPlayerController);

    if(localLife == 0){
        for(int i = 1; i < 64; i++){
            uintptr_t chunk = Rd<uintptr_t>(entityList + 8*((i&0x7FFF)>>9) + 16);
            if(!chunk) continue;
            uintptr_t ctrl = Rd<uintptr_t>(chunk + 112*(i&0x1FF));
            if(!ctrl||ctrl==localCtrl) continue;
            if(Rd<bool>(ctrl + offsets::controller::m_bPawnIsAlive)) continue;
            uint32_t obsHandle = Rd<uint32_t>(ctrl + offsets::controller::m_hObserverPawn);
            if(!obsHandle) obsHandle = Rd<uint32_t>(ctrl + offsets::controller::m_hPlayerPawn);
            if(!obsHandle) continue;
            uintptr_t obsPawn = ResolveHandle(entityList, obsHandle);
            if(!obsPawn) continue;
            uintptr_t obsSvc = Rd<uintptr_t>(obsPawn + offsets::base_pawn::m_pObserverServices);
            if(!obsSvc) continue;
            uint32_t targetHandle = Rd<uint32_t>(obsSvc + offsets::observer::m_hObserverTarget);
            if(!targetHandle) continue;
            uintptr_t targetPawn = ResolveHandle(entityList, targetHandle);
            if(targetPawn != localPawn) continue;
            if(g_spectatorCount >= kMaxSpectators) continue;
            ReadPlayerName(ctrl, g_spectatorNames[g_spectatorCount], sizeof(g_spectatorNames[g_spectatorCount]));
            g_spectatorCount++;
        }
    }else{
        uintptr_t obsSvc = Rd<uintptr_t>(localPawn + offsets::base_pawn::m_pObserverServices);
        if(obsSvc){
            uint32_t targetHandle = Rd<uint32_t>(obsSvc + offsets::observer::m_hObserverTarget);
            if(targetHandle){
                uintptr_t targetPawn = ResolveHandle(entityList, targetHandle);
                if(targetPawn){
                    g_weAreSpectating = true;
                    for(int i = 1; i < 64; i++){
                        uintptr_t chunk = Rd<uintptr_t>(entityList + 8*((i&0x7FFF)>>9) + 16);
                        if(!chunk) continue;
                        uintptr_t ctrl = Rd<uintptr_t>(chunk + 112*(i&0x1FF));
                        if(!ctrl) continue;
                        uint32_t ph = Rd<uint32_t>(ctrl + offsets::controller::m_hPlayerPawn);
                        if(!ph) continue;
                        uintptr_t pchunk = Rd<uintptr_t>(entityList + 8*((ph&0x7FFF)>>9) + 16);
                        if(!pchunk) continue;
                        uintptr_t pawn = Rd<uintptr_t>(pchunk + 112*(ph&0x1FF));
                        if(pawn == targetPawn){
                            ReadPlayerName(ctrl, g_spectatingTarget, sizeof(g_spectatingTarget));
                            break;
                        }
                    }
                }
            }
        }
    }
}

static void DrawOverlayWatermarkChrome(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float round){
    const ImU32 colBg = IM_COL32(16,16,16,245);
    const ImU32 colBorder = IM_COL32(64,64,64,220);
    const ImU32 colInner = IM_COL32(8,8,8,210);
    dl->AddRectFilled(p0, p1, colBg, round);
    dl->AddRect(p0, p1, colBorder, round, 0, 1.f);
    dl->AddRect({p0.x+1.f,p0.y+1.f},{p1.x-1.f,p1.y-1.f}, colInner, round, 0, 1.f);
    float stripH = (std::min)(3.f, (p1.y - p0.y) * 0.22f);
    if(stripH < 1.2f) return;
    float gx0 = p0.x + 2.f;
    float gy0 = p0.y + 1.f;
    float gy1 = p0.y + stripH;
    float gq1 = p0.x + (p1.x - p0.x) * 0.33f;
    float gq2 = p0.x + (p1.x - p0.x) * 0.66f;
    float gx1 = p1.x - 2.f;
    if(gy1 > p0.y + round * 0.45f) gy1 = p0.y + round * 0.45f;
    if(gy1 <= gy0) return;
    dl->AddRectFilledMultiColor({gx0,gy0},{gq1,gy1},
        IM_COL32(108,132,188,220), IM_COL32(174,122,190,220), IM_COL32(174,122,190,220), IM_COL32(108,132,188,220));
    dl->AddRectFilledMultiColor({gq1,gy0},{gq2,gy1},
        IM_COL32(174,122,190,220), IM_COL32(194,166,118,220), IM_COL32(194,166,118,220), IM_COL32(174,122,190,220));
    dl->AddRectFilledMultiColor({gq2,gy0},{gx1,gy1},
        IM_COL32(194,166,118,220), IM_COL32(116,168,148,220), IM_COL32(116,168,148,220), IM_COL32(194,166,118,220));
}

static void DrawSpectatorList(float sw){
    if(!g_spectatorListEnabled) return;
    if(g_spectatorCount == 0 && !g_weAreSpectating) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList(); if(!dl) return;
    ImFont* fBold = font::bold    ? font::bold    : ImGui::GetFont();
    ImFont* fReg  = font::regular ? font::regular : ImGui::GetFont();

    const float margin  = 15.f;
    const float padX    = 12.f;
    const float padY    = 10.f;
    const float rnd     = 8.f;
    const float lineH   = 22.f;
    const float headerH = 30.f;

    float yBase = margin;
    if(g_watermarkEnabled){
        yBase += 34.f + 8.f;
    }

    const ImU32 colAccent  = IM_COL32((int)(g_accentColor[0]*255),(int)(g_accentColor[1]*255),(int)(g_accentColor[2]*255),255);
    const ImU32 colAccentD = IM_COL32((int)(g_accentColor[0]*160),(int)(g_accentColor[1]*160),(int)(g_accentColor[2]*160),255);
    const ImU32 colText    = IM_COL32(215, 220, 230, 255);
    const ImU32 colDim     = IM_COL32(100, 105, 118, 255);
    const ImU32 colSep     = IM_COL32(32, 34, 42, 255);
    const ImU32 colRowHov  = IM_COL32(255, 255, 255, 8);

    if(g_weAreSpectating && g_spectatingTarget[0]){
        ImVec2 szTarget = fReg->CalcTextSizeA(fReg->LegacySize, FLT_MAX, 0.f, g_spectatingTarget);
        const char* prefix = "WATCHING";
        ImVec2 szPrefix  = fBold->CalcTextSizeA(fBold->LegacySize, FLT_MAX, 0.f, prefix);
        float pillW = padX + szPrefix.x + 8.f + szTarget.x + padX;
        pillW = (std::max)(pillW, 140.f);
        float pillH = headerH;
        float px = sw - pillW - margin;
        float py = yBase;
        DrawOverlayWatermarkChrome(dl, {px, py}, {px+pillW, py+pillH}, rnd);
        dl->AddRectFilled({px, py+4.f}, {px+2.f, py+pillH-4.f}, colAccent, 2.f);
        float midY = py + pillH * 0.5f;
        dl->AddText(fBold, fBold->LegacySize, {px+padX, midY - szPrefix.y*0.5f}, colAccent, prefix);
        dl->AddText(fReg,  fReg->LegacySize,  {px+padX+szPrefix.x+8.f, midY - szTarget.y*0.5f}, colText, g_spectatingTarget);
        return;
    }

    if(g_spectatorCount <= 0) return;

    float maxNameW = 60.f;
    for(int i = 0; i < g_spectatorCount; i++){
        ImVec2 ts = fReg->CalcTextSizeA(fReg->LegacySize, FLT_MAX, 0.f, g_spectatorNames[i]);
        if(ts.x > maxNameW) maxNameW = ts.x;
    }

    const char* hdrLabel = "SPECTATORS";
    ImVec2 szHdr = fBold->CalcTextSizeA(fBold->LegacySize, FLT_MAX, 0.f, hdrLabel);
    char cntBuf[8]; std::snprintf(cntBuf, sizeof(cntBuf), "%d", g_spectatorCount);
    ImVec2 szCnt = fReg->CalcTextSizeA(fReg->LegacySize, FLT_MAX, 0.f, cntBuf);
    float badgeW = szCnt.x + 10.f;
    float badgeH = 16.f;

    float boxW = (std::max)(maxNameW + padX*2.f, szHdr.x + padX*2.f + badgeW + 6.f + padX);
    boxW = (std::max)(boxW, 130.f);
    float boxH = headerH + 1.f + (float)g_spectatorCount * lineH + padY;

    float x = sw - boxW - margin;
    float y = yBase;

    dl->AddRectFilled({x-3.f, y-3.f}, {x+boxW+3.f, y+boxH+3.f}, IM_COL32(0,0,0,55), rnd+2.f);
    DrawOverlayWatermarkChrome(dl, {x, y}, {x+boxW, y+boxH}, rnd);
    dl->AddRectFilled({x+1.f, y+1.f}, {x+boxW-1.f, y+headerH}, IM_COL32(10,10,14,140), rnd, ImDrawFlags_RoundCornersTop);
    dl->AddLine({x+2.f, y+headerH}, {x+boxW-2.f, y+headerH}, colSep, 1.f);
    dl->AddRectFilled({x, y+4.f}, {x+2.f, y+boxH-4.f}, colAccent, 2.f);

    float hMid = y + headerH * 0.5f;
    dl->AddText(fBold, fBold->LegacySize, {x+padX, hMid - szHdr.y*0.5f}, colAccent, hdrLabel);

    float badgeX = x + boxW - padX - badgeW;
    float badgeY = hMid - badgeH * 0.5f;
    dl->AddRectFilled({badgeX, badgeY}, {badgeX+badgeW, badgeY+badgeH}, colAccentD, badgeH*0.5f);
    dl->AddText(fReg, fReg->LegacySize, {badgeX + (badgeW-szCnt.x)*0.5f, badgeY + (badgeH-szCnt.y)*0.5f}, IM_COL32(255,255,255,230), cntBuf);

    for(int i = 0; i < g_spectatorCount; i++){
        float ey = y + headerH + 1.f + (float)i * lineH;
        float eMid = ey + lineH * 0.5f;

        if(i % 2 == 0)
            dl->AddRectFilled({x+1.f, ey}, {x+boxW-1.f, ey+lineH}, colRowHov);

        char idxBuf[4]; std::snprintf(idxBuf, sizeof(idxBuf), "%d", i+1);
        ImVec2 szIdx = fReg->CalcTextSizeA(fReg->LegacySize, FLT_MAX, 0.f, idxBuf);
        dl->AddText(fReg, fReg->LegacySize, {x+padX, eMid - szIdx.y*0.5f}, colDim, idxBuf);

        dl->AddText(fReg, fReg->LegacySize, {x+padX+18.f, eMid - szIdx.y*0.5f}, colText, g_spectatorNames[i]);
    }
}

static void ProcessHitEvents(){
    std::fill(std::begin(g_seenThisFrame), std::end(g_seenThisFrame), false);
    for(int i=0;i<g_esp_count;i++){
        const ESPEntry& e = g_esp_players[i];
        if(!e.valid) continue;
        if(e.ent_index <= 0 || e.ent_index > ESP_MAX_PLAYERS) continue;
        if(e.team == g_esp_local_team) continue;
        g_seenThisFrame[e.ent_index] = true;
        int prev = g_lastHealth[e.ent_index];
        if(prev > 0 && e.health < prev){
            int dmg = prev - e.health;
            char buf[256];
            std::snprintf(buf,sizeof(buf),"Hit %s for %d", e.name[0]?e.name:"Enemy", dmg);
            if(g_hitNotifEnabled) PushNotification(buf, IM_COL32(240,180,60,255));
            LogEntry le{}; std::snprintf(le.text,sizeof(le.text),"%s",buf); le.color=IM_COL32(240,180,60,255); le.maxlife=4.f; le.lifetime=4.f; le.type=0;
            g_logs.push_back(le); if(g_logs.size()>8)g_logs.pop_front();
            PlayHitSound(g_hitSoundType);
            if(g_damageFloatersEnabled){
                DamageFloater df{};
                df.damage = dmg;
                df.spawnMs = GetTickCount64();
                df.ax = e.head_x;
                df.ay = e.head_y;
                df.duration = g_damageFloaterDuration;
                df.wx = e.head_ox;
                df.wy = e.head_oy;
                df.wz = e.head_oz + 8.f;
                UINT64 j = GetTickCount64() ^ (UINT64)(uintptr_t)e.pawn ^ ((UINT64)e.ent_index << 17);
                df.randOffX = ((j & 0xFFF) / 4095.f) * 16.f - 8.f;
                g_damageFloaters.push_back(df);
                while(g_damageFloaters.size() > 32) g_damageFloaters.pop_front();
            }
        }
        if(prev > 0 && e.health <= 0){
            char buf[256];
            std::snprintf(buf,sizeof(buf),"Killed %s", e.name[0]?e.name:"Enemy");
            if(g_killNotifEnabled) PushNotification(buf, IM_COL32(140,100,255,255));
            LogEntry le{}; std::snprintf(le.text,sizeof(le.text),"%s",buf); le.color=IM_COL32(140,100,255,255); le.maxlife=4.f; le.lifetime=4.f; le.type=1;
            g_logs.push_back(le); if(g_logs.size()>8)g_logs.pop_front();
            if(g_killEffectEnabled){
                g_lastKillEffectTime = GetTickCount64();
                g_lastKillEffectPos = {e.head_ox, e.head_oy, e.head_oz};
                g_pendingKillParticles = true;
            }
        }
        g_lastHealth[e.ent_index] = e.health;
    }
    for(int idx=1; idx<=ESP_MAX_PLAYERS; ++idx){
        if(!g_seenThisFrame[idx]) g_lastHealth[idx] = 0;
    }
}

static void RunNoFlash(){
    if(!g_noFlash||!g_client)return;
    __try{
        uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);
        if(!lp||!IsLikelyPtr(lp))return;
        uintptr_t flashAddr=lp+offsets::cs_pawn_base::m_flFlashDuration;
        if(!IsLikelyPtr((uintptr_t)flashAddr))return;
        float dur=Rd<float>(flashAddr);
        if(dur>0.01f){
            Wr<float>(flashAddr,0.f);
            Wr<float>(lp+offsets::cs_pawn_base::m_flFlashMaxAlpha,0.f);
        }
    }__except(EXCEPTION_EXECUTE_HANDLER){}
}

static void RunNoSmoke(){
    if(!g_noSmoke||!g_client) return;
    __try{
        uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);
        if(lp&&IsLikelyPtr(lp)) Wr<float>(lp+offsets::cs_pawn_base::m_flLastSmokeOverlayAlpha,0.f);
        UINT64 now = GetTickCount64();
        if(now - g_lastNoSmokeTick < 100) return;
        g_lastNoSmokeTick = now;
        uintptr_t entityList=Rd<uintptr_t>(g_client+offsets::client::dwEntityList);if(!entityList)return;
        for(int i=0;i<2048;i++){
            uintptr_t chunk=Rd<uintptr_t>(entityList+8*((i&0x7FFF)>>9)+16);if(!chunk)continue;
            uintptr_t ent=Rd<uintptr_t>(chunk+112*(i&0x1FF));if(!ent||!IsLikelyPtr(ent))continue;
            __try{
                uint8_t spawned=Rd<uint8_t>(ent+offsets::smoke_projectile::m_bSmokeEffectSpawned);
                if(!spawned)continue;
                uintptr_t alphaProp=Rd<uintptr_t>(ent+offsets::model_entity::m_pClientAlphaProperty);
                if(alphaProp&&IsLikelyPtr(alphaProp)) Wr<uint8_t>(alphaProp+offsets::client_alpha_prop::m_nAlpha,0);
            }__except(EXCEPTION_EXECUTE_HANDLER){}
        }
    }__except(EXCEPTION_EXECUTE_HANDLER){}
}

static void RunGlow(){
    static bool s_wasActive = false;
    if(!g_client) return;
    bool needGlowPass = g_glowEnabled || (g_chamsEnabled && !g_chamsScene);
    if(!needGlowPass){
        if(s_wasActive){
            uintptr_t entityList=Rd<uintptr_t>(g_client+offsets::client::dwEntityList);
            if(entityList){
                for(int i=1;i<64;i++){
                    uintptr_t chunk=Rd<uintptr_t>(entityList+8*((i&0x7FFF)>>9)+16);if(!chunk)continue;
                    uintptr_t ctrl=Rd<uintptr_t>(chunk+112*(i&0x1FF));if(!ctrl)continue;
                    uint32_t ph=Rd<uint32_t>(ctrl+offsets::controller::m_hPlayerPawn);if(!ph)continue;
                    uintptr_t pchunk=Rd<uintptr_t>(entityList+8*((ph&0x7FFF)>>9)+16);if(!pchunk)continue;
                    uintptr_t pawn=Rd<uintptr_t>(pchunk+112*(ph&0x1FF));
                    if(!pawn)continue;
                    uintptr_t glowProp=pawn+offsets::model_entity::m_Glow;
                    Wr<uint8_t>(glowProp+offsets::glow_prop::m_bGlowing,0);
                }
            }
        }
        s_wasActive = false;
        return;
    }
    s_wasActive = true;
    uintptr_t entityList=Rd<uintptr_t>(g_client+offsets::client::dwEntityList);if(!entityList)return;
    uintptr_t localPawn=g_esp_local_pawn;int localTeam=g_esp_local_team;
    for(int i=1;i<64;i++){
        uintptr_t chunk=Rd<uintptr_t>(entityList+8*((i&0x7FFF)>>9)+16);if(!chunk)continue;
        uintptr_t ctrl=Rd<uintptr_t>(chunk+112*(i&0x1FF));
        if(!ctrl||!Rd<bool>(ctrl+offsets::controller::m_bPawnIsAlive))continue;
        uint32_t ph=Rd<uint32_t>(ctrl+offsets::controller::m_hPlayerPawn);if(!ph)continue;
        uintptr_t pchunk=Rd<uintptr_t>(entityList+8*((ph&0x7FFF)>>9)+16);if(!pchunk)continue;
        uintptr_t pawn=Rd<uintptr_t>(pchunk+112*(ph&0x1FF));
        if(!pawn||pawn==localPawn)continue;
        int health=Rd<int>(pawn+offsets::base_entity::m_iHealth);if(health<=0)continue;
        int team=(int)Rd<uint8_t>(pawn+offsets::base_entity::m_iTeamNum);
        bool isTeam = (team == localTeam);
        bool applyChams = g_chamsEnabled && !g_chamsScene && (!g_chamsEnemyOnly || !isTeam);
        bool applyGlow = g_glowEnabled;
        bool apply = applyChams || applyGlow;
        uintptr_t glowProp=pawn+offsets::model_entity::m_Glow;
        if(apply){
            float* col = nullptr;
            if(applyChams){
                if(!isTeam && g_chamsIgnoreZ && i > 0 && i <= ESP_MAX_PLAYERS && !g_visMap[i]){
                    col = g_chamsIgnoreZCol;
                }else{
                    col = isTeam ? g_chamsTeamCol : g_chamsEnemyCol;
                }
            }else{
                col = isTeam ? g_glowTeamCol : g_glowEnemyCol;
            }
            float tmp[4]{col[0], col[1], col[2], col[3]};
            if(applyChams){
                ApplyChamsMaterial(tmp);
            }else if(isTeam){
                float sumRgb = tmp[0] + tmp[1] + tmp[2];
                tmp[3] = (sumRgb < 1e-4f) ? 0.f : Clampf(g_glowAlpha, 0.f, 1.f);
            }else{
                tmp[3] = Clampf(tmp[3] * g_glowAlpha, 0.f, 1.f);
            }
            MaterialColor gc = MakeMatColor(tmp);
            Wr<MaterialColor>(glowProp+offsets::glow_prop::m_glowColorOverride,gc);
            Wr<uint8_t>(glowProp+offsets::glow_prop::m_bGlowing,1);
        }else{
            Wr<uint8_t>(glowProp+offsets::glow_prop::m_bGlowing,0);
        }
    }
}

static void RunRadarHack(){
    if(!g_radarIngame||!g_client)return;
    uintptr_t entityList=Rd<uintptr_t>(g_client+offsets::client::dwEntityList);if(!entityList)return;
    uintptr_t localPawn=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);if(!localPawn)return;
    int localTeam=(int)Rd<uint8_t>(localPawn+offsets::base_entity::m_iTeamNum);
    for(int i=1;i<64;i++){
        uintptr_t chunk=Rd<uintptr_t>(entityList+8*((i&0x7FFF)>>9)+16);if(!chunk)continue;
        uintptr_t ctrl=Rd<uintptr_t>(chunk+112*(i&0x1FF));
        if(!ctrl||!Rd<bool>(ctrl+offsets::controller::m_bPawnIsAlive))continue;
        uint32_t ph=Rd<uint32_t>(ctrl+offsets::controller::m_hPlayerPawn);if(!ph)continue;
        uintptr_t pchunk=Rd<uintptr_t>(entityList+8*((ph&0x7FFF)>>9)+16);if(!pchunk)continue;
        uintptr_t pawn=Rd<uintptr_t>(pchunk+112*(ph&0x1FF));
        if(!pawn||pawn==localPawn)continue;
        int health=Rd<int>(pawn+offsets::base_entity::m_iHealth);if(health<=0)continue;
        int team=(int)Rd<uint8_t>(pawn+offsets::base_entity::m_iTeamNum);
        if(team==localTeam)continue;
        uintptr_t spotBase=pawn+offsets::spotted::m_entitySpottedState;
        Wr<uint8_t>(spotBase+offsets::spotted::m_bSpotted, 1);
        Wr<uint32_t>(spotBase+offsets::spotted::m_bSpottedByMask, 0xFFFFFFFF);
        Wr<uint32_t>(spotBase+offsets::spotted::m_bSpottedByMask+4, 0xFFFFFFFF);
    }
}

static void EnsureSkinRegen(){
    if(g_regenSkinsReady) return;
    g_regenSkinsReady = true;
    HMODULE client = GetModuleHandleA("client.dll");
    if(!client) return;
    static const char PAT_REGEN[] = "\x48\x83\xEC\x00\xE8\x00\x00\x00\x00\x48\x85\xC0\x0F\x84\x00\x00\x00\x00\x48\x8B\x10";
    static const char MSK_REGEN[] = "xxx?x????xxxxx????xxx";
    void* fn = PatternScan(client, PAT_REGEN, MSK_REGEN);
    if(fn) g_regenSkins = reinterpret_cast<RegenerateWeaponSkinsFn>(fn);
}

static void RunSkinChanger(){
    if(!g_client) return;
    if(!g_skinEnabled){
        if(g_skinForceUpdate){
            g_skinForceUpdate = false;
            EnsureSkinRegen();
            if(g_regenSkins){
                __try{ g_regenSkins(); }__except(EXCEPTION_EXECUTE_HANDLER){}
            }
        }
        return;
    }
    uintptr_t entityList = Rd<uintptr_t>(g_client + offsets::client::dwEntityList);
    if(!entityList) return;
    uintptr_t lp = Rd<uintptr_t>(g_client + offsets::client::dwLocalPlayerPawn);
    if(!lp) return;
    if(g_skinActiveOnly){
        uintptr_t weapon = GetActiveWeapon(lp, entityList);
        if(weapon){
            int wId = GetWeaponId(weapon);
            if(SkinOverride* o = FindSkinOverride(wId)) ApplySkinToWeapon(weapon, *o);
        }
    }else{
        static std::vector<uintptr_t> weapons;
        CollectWeapons(lp, entityList, weapons);
        for(uintptr_t w : weapons){
            int wId = GetWeaponId(w);
            if(SkinOverride* o = FindSkinOverride(wId)) ApplySkinToWeapon(w, *o);
        }
    }
    if(g_skinForceUpdate){
        g_skinForceUpdate = false;
        EnsureSkinRegen();
        if(g_regenSkins){
            __try{ g_regenSkins(); }__except(EXCEPTION_EXECUTE_HANDLER){}
        }
    }
}

static void RunAutostop(){
    if(!g_autostopEnabled||!g_client||g_menuOpen){ ClearAutostopInput(); return; }
    bool aimHeld = (GetAsyncKeyState(g_aimbotKey) & 0x8000) != 0;
    bool fireHeld = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if(!aimHeld || !fireHeld){ ClearAutostopInput(); return; }
    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);if(!lp){ ClearAutostopInput(); return; }
    int shots = Rd<int>(lp + offsets::cs_pawn::m_iShotsFired);
    if(shots > 0){ ClearAutostopInput(); return; }
    uintptr_t vaAddr = ViewAnglesAddr();
    if(!vaAddr){ ClearAutostopInput(); return; }
    __try{
        Vec3 vel = Rd<Vec3>(lp + offsets::base_entity::m_vecVelocity);
        float yawDeg = Rd<float>(vaAddr + 4);
        float yaw = yawDeg * (3.14159265f / 180.f);
        float cosY = cosf(yaw), sinY = sinf(yaw);
        float forward = vel.x * cosY + vel.y * sinY;
        float side = -vel.x * sinY + vel.y * cosY;
        const float dz = 12.f;
        if(fabsf(forward) < dz && fabsf(side) < dz){ ClearAutostopInput(); return; }
        Wr<int>(g_client + offsets::buttons::forward, forward < -dz ? 65537 : 0);
        Wr<int>(g_client + offsets::buttons::back,    forward > dz ? 65537 : 0);
        Wr<int>(g_client + offsets::buttons::left,    side > dz ? 65537 : 0);
        Wr<int>(g_client + offsets::buttons::right,   side < -dz ? 65537 : 0);
        g_autostopOwned = true;
    }__except(EXCEPTION_EXECUTE_HANDLER){}
}

static void DrawDebugConsole() {
    if (!g_showDebugConsole) return;
    ImGui::SetNextWindowSize({ 500, 400 }, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Debug Console", &g_showDebugConsole)) {
        if (ImGui::Button("Clear")) ClearDebugLogs();
        ImGui::Separator();
        ImGui::BeginChild("LogScroll");
        auto logs = GetDebugLogs();
        for (const auto& log : logs) {
            ImGui::TextUnformatted(log.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }
    ImGui::End();
}

static void RunBHop(){
    if(!g_bhopEnabled||!g_client)return;
    if(g_menuOpen){ Wr<int>(g_client+offsets::buttons::jump, 0); return; }

    bool spaceHeld = (GetAsyncKeyState(VK_SPACE)&0x8000) != 0;
    if(!spaceHeld){ Wr<int>(g_client+offsets::buttons::jump, 0); return; }

    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn); if(!lp)return;
    bool onGround = (Rd<uint32_t>(lp+offsets::base_entity::m_fFlags) & 1) != 0;

    Wr<int>(g_client+offsets::buttons::jump, onGround ? 65537 : 0);
}




static void RunFOVChanger(){
    if(!g_fovEnabled||!g_client)return;
    if(g_origGetWorldFov) return;
    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);if(!lp)return;
    bool scoped=Rd<bool>(lp+offsets::cs_pawn::m_bIsScoped);if(scoped)return;
    uintptr_t camSvc=Rd<uintptr_t>(lp+offsets::base_pawn::m_pCameraServices);if(!camSvc)return;
    Wr<float>(camSvc+offsets::camera::m_iFOV,g_fovValue);
}

static void RunRCS(){
    if(!g_rcsEnabled||!g_client)return;
    if(g_menuOpen)return;
    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);if(!lp)return;

    float punchX=Rd<float>(lp+offsets::cs_pawn::m_aimPunchAngle);
    float punchY=Rd<float>(lp+offsets::cs_pawn::m_aimPunchAngle+4);

    bool shooting=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
    int shots=Rd<int>(lp+offsets::cs_pawn::m_iShotsFired);

    if(!shooting || shots<1){
        g_rcsPrevPunchX=punchX;
        g_rcsPrevPunchY=punchY;
        return;
    }

    float dx=(punchX-g_rcsPrevPunchX)*g_rcsX;
    float dy=(punchY-g_rcsPrevPunchY)*g_rcsY;
    g_rcsPrevPunchX=punchX;
    g_rcsPrevPunchY=punchY;

    if(dx==0.f&&dy==0.f)return;

    uintptr_t vaAddr=ViewAnglesAddr();if(!vaAddr)return;
    float pitch=Rd<float>(vaAddr);float yaw=Rd<float>(vaAddr+4);
    pitch-=dx*2.f;
    yaw -=dy*2.f;
    pitch=Clampf(pitch,-89.f,89.f);
    if(yaw>180.f)yaw-=360.f;else if(yaw<-180.f)yaw+=360.f;
    Wr<float>(vaAddr,pitch);
    Wr<float>(vaAddr+4,yaw);
}

static void RunStrafeHelper(){
    if(!g_strafeEnabled||!g_client||g_menuOpen){ ClearStrafeInput(); return; }
    if(g_strafeKey!=0&&!(GetAsyncKeyState(g_strafeKey)&0x8000)){ ClearStrafeInput(); return; }
    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn); if(!lp){ ClearStrafeInput(); return; }
    if(Rd<uint32_t>(lp+offsets::base_entity::m_fFlags)&1){ ClearStrafeInput(); return; }
    
    uintptr_t vaAddr=ViewAnglesAddr(); if(!vaAddr){ ClearStrafeInput(); return; }
    float curYaw=Rd<float>(vaAddr+4);
    static float s_lastYaw = 0.f;
    float delta = curYaw - s_lastYaw;
    if(delta > 180.f) delta -= 360.f; else if(delta < -180.f) delta += 360.f;
    s_lastYaw = curYaw;

    if(fabsf(delta) < 0.1f){ ClearStrafeInput(); return; }

    if(delta > 0.f){
        Wr<int>(g_client+offsets::buttons::left,  65537);
        Wr<int>(g_client+offsets::buttons::right, 0);
    } else {
        Wr<int>(g_client+offsets::buttons::right, 65537);
        Wr<int>(g_client+offsets::buttons::left,  0);
    }
    g_strafeOwned = true;
}

static constexpr int kEntityListStride = 112;
static void RunTriggerBot(){
    if(!g_tbEnabled||!g_client||g_menuOpen){ ClearTriggerInput(); return; }
    if(g_tbKey!=0&&!(GetAsyncKeyState(g_tbKey)&0x8000)){ ClearTriggerInput(); return; }
    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);if(!lp){ ClearTriggerInput(); return; }
    int entIdx=Rd<int>(lp+offsets::cs_pawn::m_iIDEntIndex);
    if(entIdx<=0||entIdx>8192){ ClearTriggerInput(); return; }
    uintptr_t entityList=Rd<uintptr_t>(g_client+offsets::client::dwEntityList);if(!entityList){ ClearTriggerInput(); return; }
    uintptr_t pchunk=Rd<uintptr_t>(entityList+8*((entIdx&0x7FFF)>>9)+16);if(!pchunk){ ClearTriggerInput(); return; }
    uintptr_t targPawn=Rd<uintptr_t>(pchunk+kEntityListStride*(entIdx&0x1FF));
    if(!targPawn||!IsLikelyPtr(targPawn)){ ClearTriggerInput(); return; }
    int lifeState=Rd<uint8_t>(targPawn+offsets::base_entity::m_lifeState);
    if(lifeState!=0){ ClearTriggerInput(); return; }
    int targTeam=(int)Rd<uint8_t>(targPawn+offsets::base_entity::m_iTeamNum);
    int targHealth=Rd<int>(targPawn+offsets::base_entity::m_iHealth);
    if(targHealth<=0){ ClearTriggerInput(); return; }
    if(g_tbTeamChk&&targTeam==g_esp_local_team){ ClearTriggerInput(); return; }
    if(!g_tbShouldFire){g_tbShouldFire=true;g_tbFireTime=GetTickCount64()+(UINT64)g_tbDelay;}
    if(GetTickCount64()>=g_tbFireTime){
        Wr<int>(g_client+offsets::buttons::attack,65537);
        g_tbShouldFire=false;
        g_tbJustFired=true;
        g_tbHoldFramesLeft=4;
    }
}

static void ReleaseTriggerAttack(){
    if(!g_client||!g_tbEnabled||g_menuOpen){ ClearTriggerInput(); return; }
    if(g_tbJustFired && g_tbHoldFramesLeft>0){
        Wr<int>(g_client+offsets::buttons::attack,65537);
        g_tbHoldFramesLeft--;
        return;
    }
    if(g_tbJustFired){
        Wr<int>(g_client+offsets::buttons::attack,256);
        g_tbJustFired=false;
        return;
    }
}

static void RunAimbot(){
    g_aimbotLastFound = false;
    g_aimbotLastBestFov = 1e9f;
    if(!g_aimbotEnabled||!g_client)return;
    if(g_menuOpen) return;
    if(!(GetAsyncKeyState(g_aimbotKey)&0x8000))return;
    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);if(!lp)return;
    bool lmbHeld = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    int shotsFired = Rd<int>(lp + offsets::cs_pawn::m_iShotsFired);
    if(g_rcsEnabled && lmbHeld && shotsFired >= 1) return;
    uintptr_t vaAddr=ViewAnglesAddr();if(!vaAddr)return;
    uintptr_t sc0=Rd<uintptr_t>(lp+offsets::base_entity::m_pGameSceneNode);Vec3 localOrigin{};
    if(sc0)localOrigin=Rd<Vec3>(sc0+offsets::scene_node::m_vecAbsOrigin);
    Vec3 eyePos=localOrigin+Rd<Vec3>(lp+offsets::base_pawn::m_vecViewOffset);
    float curPitch=Rd<float>(vaAddr);float curYaw=Rd<float>(vaAddr+4);
    float bestDist=g_aimbotFov;Vec3 bestPoint{};bool found=false;
    auto evalPoint = [&](const Vec3& p){
        Vec2 aimAngle=CalcAngle(eyePos,p);
        float dPitch=fabsf(AngleDiff(aimAngle.x,curPitch));
        float dYaw=fabsf(AngleDiff(aimAngle.y,curYaw));
        float fovDist=sqrtf(dPitch*dPitch+dYaw*dYaw);
        if(fovDist<bestDist){bestDist=fovDist;bestPoint=p;found=true;}
    };
    uintptr_t crossPawn = 0;
    if(g_aimbotVisCheck){
        int crossIdx = Rd<int>(lp + offsets::cs_pawn::m_iIDEntIndex);
        if(crossIdx > 0 && crossIdx <= 8192){
            uintptr_t entityList = Rd<uintptr_t>(g_client + offsets::client::dwEntityList);
            if(entityList){
                uintptr_t pchunk = Rd<uintptr_t>(entityList + 8*((crossIdx&0x7FFF)>>9) + 16);
                if(pchunk)
                    crossPawn = Rd<uintptr_t>(pchunk + kEntityListStride * (crossIdx & 0x1FF));
            }
        }
        if(!crossPawn || !IsLikelyPtr(crossPawn)) return;
        int ls = Rd<uint8_t>(crossPawn + offsets::base_entity::m_lifeState);
        if(ls != 0) return;
        if(Rd<int>(crossPawn + offsets::base_entity::m_iHealth) <= 0) return;
        if(g_aimbotTeamChk && (int)Rd<uint8_t>(crossPawn + offsets::base_entity::m_iTeamNum) == g_esp_local_team) return;
    }
    for(int i=0;i<g_esp_count;i++){
        const ESPEntry&e=g_esp_players[i];
        if(!e.valid||!e.pawn||!IsLikelyPtr(e.pawn))continue;
        if(g_aimbotTeamChk&&e.team==g_esp_local_team)continue;
        if(e.distance>g_espMaxDist)continue;
        if(g_aimbotVisCheck && e.pawn != crossPawn)continue;
        UpdatePawnBones(e.pawn);
        auto getBone = [&](int id, Vec3& out) -> bool {
            return GetBonePos(e.pawn, id, out);
        };
        Vec3 aimPoint{e.head_ox, e.head_oy, e.head_oz};
        { Vec3 bp{}; if(getBone(BONE_HEAD,bp)) aimPoint=bp; }
        evalPoint(aimPoint);
    }
    if(!found){
        uintptr_t entityList=Rd<uintptr_t>(g_client+offsets::client::dwEntityList);
        int localTeam=(int)Rd<uint8_t>(lp+offsets::base_entity::m_iTeamNum);
        uintptr_t localCtrl=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerController);
        if(entityList){
            for(int i=1;i<64;i++){
                uintptr_t chunk=Rd<uintptr_t>(entityList+8*((i&0x7FFF)>>9)+16); if(!chunk)continue;
                uintptr_t ctrl=Rd<uintptr_t>(chunk+kEntityListStride*(i&0x1FF)); if(!ctrl||!IsLikelyPtr(ctrl))continue;
                if(ctrl==localCtrl)continue;
                if(!Rd<bool>(ctrl+offsets::controller::m_bPawnIsAlive))continue;
                uint32_t ph=Rd<uint32_t>(ctrl+offsets::controller::m_hPlayerPawn); if(!ph)continue;
                uintptr_t pchunk=Rd<uintptr_t>(entityList+8*((ph&0x7FFF)>>9)+16); if(!pchunk)continue;
                uintptr_t pawn=Rd<uintptr_t>(pchunk+kEntityListStride*(ph&0x1FF)); if(!pawn||!IsLikelyPtr(pawn)||pawn==lp)continue;
                if(Rd<uint8_t>(pawn+offsets::base_entity::m_lifeState)!=0)continue;
                if(Rd<int>(pawn+offsets::base_entity::m_iHealth)<=0)continue;
                if(g_aimbotTeamChk && (int)Rd<uint8_t>(pawn+offsets::base_entity::m_iTeamNum)==localTeam)continue;
                if(g_aimbotVisCheck && pawn != crossPawn)continue;
                Vec3 origin=Rd<Vec3>(pawn+offsets::base_pawn::m_vOldOrigin);
                uintptr_t scn=Rd<uintptr_t>(pawn+offsets::base_entity::m_pGameSceneNode); if(scn) origin=Rd<Vec3>(scn+offsets::scene_node::m_vecAbsOrigin);
                Vec3 viewOff=Rd<Vec3>(pawn+offsets::base_pawn::m_vecViewOffset);
                Vec3 head={origin.x+viewOff.x, origin.y+viewOff.y, origin.z+viewOff.z};
                UpdatePawnBones(pawn);
                Vec3 aimPoint = head;
                { Vec3 bp{}; if(GetBonePos(pawn, BONE_HEAD, bp)) aimPoint = bp; }
                float dist=(aimPoint-localOrigin).length()/100.f; if(dist>g_espMaxDist)continue;
                evalPoint(aimPoint);
            }
        }
    }
    if(!found)return;
    g_aimbotLastFound = true;
    g_aimbotLastBestFov = bestDist;
    Vec2 targetAngle=CalcAngle(eyePos,bestPoint);
    float smooth=Clampf(g_aimbotSmooth,1.f,50.f);
    float dp=AngleDiff(targetAngle.x,curPitch);
    float dy=AngleDiff(targetAngle.y,curYaw);
    float newPitch=curPitch+(dp/smooth);
    float newYaw=curYaw+(dy/smooth);
    newPitch=Clampf(newPitch,-89.f,89.f);
    if(newYaw>180.f)newYaw-=360.f;else if(newYaw<-180.f)newYaw+=360.f;
    Wr<float>(vaAddr,newPitch);Wr<float>(vaAddr+4,newYaw);

}


static void RunDoubleTap(){
    if(!g_dtEnabled||!g_client||g_menuOpen){ ClearDtInput(); return; }
    if(g_dtKey==0||!(GetAsyncKeyState(g_dtKey)&0x8000)){ ClearDtInput(); return; }
    Wr<int>(g_client+offsets::buttons::attack,65537);
    g_dtOwned = true;
}

static void RunAimFireGate(){
    if(!g_waitAimThenFire || !g_aimbotEnabled || !g_client || g_menuOpen) return;
    if(!(GetAsyncKeyState(g_aimbotKey) & 0x8000)) return;
    if(!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) return;
    uintptr_t lp = Rd<uintptr_t>(g_client + offsets::client::dwLocalPlayerPawn);
    if(!lp) return;
    int shots = Rd<int>(lp + offsets::cs_pawn::m_iShotsFired);
    if(g_rcsEnabled && shots >= 1) return;
    if(g_aimbotLastFound && g_aimbotLastBestFov <= g_waitAimFovDeg) return;
    Wr<int>(g_client + offsets::buttons::attack, 0);
}

struct Particle{
    float x,y,vx,vy,size,lifetime,maxlife,phase;
    float rot, spin;
    ImU32 color;int type;
    Vec3 worldPos;bool is3D;
    Vec3 worldVel;
};
static std::vector<Particle>g_particles;
static std::mt19937 g_rng{12345u};
static float Randf(float lo,float hi){
    std::uniform_real_distribution<float>d(lo,hi);return d(g_rng);
}

struct BulletTrace{Vec3 start,end;float lifetime,maxlife;ImU32 color;};
static std::deque<BulletTrace>g_traces;
static int g_lastShotsFired = 0;

struct SoundPing{Vec3 pos;float lifetime,maxlife;float radius;float height;};
static std::deque<SoundPing>g_soundPings;

static bool g_bombActive=false;
static int g_bombSite=-1;
static Vec3 g_bombPos{};
static float g_bombExplodeTime=0.f;
static float g_bombDefuseEnd=0.f;
static UINT64 g_lastBombScanMs=0;

static void PushNotification(const char*text,ImU32 color){
    (void)color;
    if(!text||!text[0])return;
}

static void PlayHitSound(int type){
    if(!g_hitSoundEnabled||type==0)return;
    switch(type){
        case 1: Beep(1000,40); break;
        case 2: Beep(1400,30); break;
        case 3: Beep(800,60); break;
        default: Beep(1100,35); break;
    }
}

static void PushLog(const char* text, ImU32 color){
    if(!text||!text[0]) return;
    LogEntry e{};
    std::snprintf(e.text,sizeof(e.text),"%s",text);
    e.color=color;
    e.maxlife=4.0f; e.lifetime=4.0f;
    g_logs.push_back(e);
    if(g_logs.size()>8) g_logs.pop_front();
}

static Vec3 AngleToForward(float pitch,float yaw){
    float p = pitch*(3.14159265f/180.f);
    float y = yaw*(3.14159265f/180.f);
    float cp = cosf(p), sp = sinf(p);
    float cy = cosf(y), sy = sinf(y);
    return {cp*cy, cp*sy, -sp};
}

static void UpdateBulletTraces(){
    if(!g_bulletTraceEnabled||!g_client) return;
    uintptr_t lp=Rd<uintptr_t>(g_client+offsets::client::dwLocalPlayerPawn);if(!lp)return;
    int shots=Rd<int>(lp+offsets::cs_pawn::m_iShotsFired);
    if(shots>g_lastShotsFired){
        uintptr_t sc0=Rd<uintptr_t>(lp+offsets::base_entity::m_pGameSceneNode);
        Vec3 origin{};if(sc0)origin=Rd<Vec3>(sc0+offsets::scene_node::m_vecAbsOrigin);
        Vec3 eye=origin+Rd<Vec3>(lp+offsets::base_pawn::m_vecViewOffset);
        uintptr_t vaAddr=ViewAnglesAddr();
        float pitch=Rd<float>(vaAddr);float yaw=Rd<float>(vaAddr+4);
        Vec3 fwd=AngleToForward(pitch,yaw);
        BulletTrace t{};t.start=eye;t.end=eye+fwd*3000.f;t.maxlife=1.0f;t.lifetime=1.0f;
        t.color=IM_COL32((int)(g_impactCol[0]*255),(int)(g_impactCol[1]*255),(int)(g_impactCol[2]*255),(int)(g_impactCol[3]*255));
        g_traces.push_back(t);
        if(g_traces.size()>24) g_traces.pop_front();
    }
    g_lastShotsFired=shots;
}

static void DrawBulletTraces(float dt){
    if(!g_bulletTraceEnabled){g_traces.clear();return;}
    if(g_traces.empty()) return;
    ImDrawList*dl=ImGui::GetForegroundDrawList();if(!dl)return;
    const float* vm = g_client ? reinterpret_cast<const float*>(g_client+offsets::client::dwViewMatrix) : nullptr;
    if(!vm) return;
    for(auto& t: g_traces){
        t.lifetime-=dt;
        float a=Clampf(t.lifetime/t.maxlife,0.f,1.f);
        float sx,sy,ex,ey;
        if(WorldToScreen(vm,t.start,g_esp_screen_w,g_esp_screen_h,sx,sy)&&WorldToScreen(vm,t.end,g_esp_screen_w,g_esp_screen_h,ex,ey)){
            uint8_t r=(t.color>>IM_COL32_R_SHIFT)&0xFF, gc=(t.color>>IM_COL32_G_SHIFT)&0xFF, b=(t.color>>IM_COL32_B_SHIFT)&0xFF;
            dl->AddLine({sx,sy},{ex,ey}, IM_COL32(r,gc,b,(int)(40*a)),  7.f);
            dl->AddLine({sx,sy},{ex,ey}, IM_COL32(r,gc,b,(int)(80*a)),  3.5f);
            dl->AddLine({sx,sy},{ex,ey}, IM_COL32(r,gc,b,(int)(220*a)), 1.2f);
            dl->AddCircleFilled({ex,ey}, 3.5f*a, IM_COL32(r,gc,b,(int)(180*a)), 8);
            dl->AddCircleFilled({ex,ey}, 1.5f*a, IM_COL32(255,255,255,(int)(180*a)), 8);
        }
    }
    g_traces.erase(std::remove_if(g_traces.begin(),g_traces.end(),[](const BulletTrace& t){return t.lifetime<=0.f;}),g_traces.end());
}

static void UpdateSoundPings(){
    if(!g_soundEnabled||!g_client) return;
    if(!g_soundBlipEnemy && !g_soundBlipTeam) return;
    UINT64 nowTick = GetTickCount64();
    for(int i=0;i<g_esp_count;i++){
        const ESPEntry& e = g_esp_players[i];
        if(!e.valid) continue;
        bool isEnemy = (e.team != g_esp_local_team);
        if(isEnemy && !g_soundBlipEnemy) continue;
        if(!isEnemy && !g_soundBlipTeam) continue;
        Vec3 vel = Rd<Vec3>(e.pawn + offsets::base_entity::m_vecVelocity);
        float spd = sqrtf(vel.x*vel.x+vel.y*vel.y+vel.z*vel.z);
        if(spd>80.f && e.ent_index>0 && e.ent_index<=ESP_MAX_PLAYERS){
            if(nowTick - g_lastSoundPingTick[e.ent_index] < 350) continue;
            g_lastSoundPingTick[e.ent_index] = nowTick;
            float scale = Clampf(g_soundPuddleScale, 0.3f, 3.0f);
            float size = (18.f + Clampf(spd/250.f, 0.f, 1.f) * 18.f) * scale;
            SoundPing p{};p.pos={e.origin_x,e.origin_y,e.origin_z};p.maxlife=0.5f;p.lifetime=0.5f;p.radius=size;p.height=0.f;
            g_soundPings.push_back(p);
            if(g_soundPings.size()>32) g_soundPings.pop_front();
        }
    }
}

static void DrawSoundPings(float dt){
    if(!g_soundEnabled){g_soundPings.clear();return;}
    if(g_soundPings.empty()) return;
    ImDrawList*dl=ImGui::GetForegroundDrawList();if(!dl)return;
    const float* vm = g_client ? reinterpret_cast<const float*>(g_client+offsets::client::dwViewMatrix) : nullptr;
    if(!vm) return;
    int colR=(int)(g_soundBlipCol[0]*255), colG=(int)(g_soundBlipCol[1]*255), colB=(int)(g_soundBlipCol[2]*255);
    for(auto& p: g_soundPings){
        p.lifetime-=dt;
        float life = Clampf(p.lifetime/p.maxlife,0.f,1.f);
        float t = 1.f - life;
        float fadeIn = Clampf(t/0.08f, 0.f, 1.f);
        float fadeOut = Clampf(life/0.2f, 0.f, 1.f);
        float alpha = fadeIn * fadeOut * Clampf(g_soundPuddleAlpha, 0.f, 2.0f);
        alpha = Clampf(alpha, 0.f, 1.f);
        float grow = LerpF(0.3f, 1.f, t);
        float rad = p.radius * grow;
        const int segs = 36;
        ImVec2 pts[segs+1];
        int validCount = 0;
        for(int i=0;i<=segs;i++){
            float ang = (float)i * (6.283185f / (float)segs);
            Vec3 wp{p.pos.x + rad*cosf(ang), p.pos.y + rad*sinf(ang), p.pos.z};
            float sx, sy;
            if(WorldToScreen(vm, wp, g_esp_screen_w, g_esp_screen_h, sx, sy)){
                pts[validCount++] = {sx, sy};
            }
        }
        if(validCount < 3) continue;
        for(int ring = 9; ring >= 0; ring--){
            float rMul = 0.15f + (float)ring * 0.09f;
            int a = (int)((160 - ring*10) * alpha);
            if(a < 12) continue;
            ImU32 ringCol = IM_COL32(colR, colG, colB, (int)Clampf((float)a, 0.f, 255.f));
            ImVec2 rPts[segs+1];
            int rCnt = 0;
            for(int i=0;i<=segs;i++){
                float ang = (float)i * (6.283185f / (float)segs);
                Vec3 wp{p.pos.x + rad*rMul*cosf(ang), p.pos.y + rad*rMul*sinf(ang), p.pos.z};
                float sx, sy;
                if(WorldToScreen(vm, wp, g_esp_screen_w, g_esp_screen_h, sx, sy)){
                    rPts[rCnt++] = {sx, sy};
                }
            }
            if(rCnt >= 3){
                float thick = 1.8f + (1.f - rMul) * 1.2f;
                for(int j=1;j<rCnt;j++) dl->AddLine(rPts[j-1], rPts[j], ringCol, thick);
                if(rCnt>1) dl->AddLine(rPts[rCnt-1], rPts[0], ringCol, thick);
            }
        }
        ImU32 fillCol = IM_COL32(colR, colG, colB, (int)(90*alpha));
        dl->AddConvexPolyFilled(pts, validCount, fillCol);
        ImU32 strokeCol = IM_COL32(colR, colG, colB, (int)(255*alpha));
        for(int j=1;j<validCount;j++) dl->AddLine(pts[j-1], pts[j], strokeCol, 2.5f);
        if(validCount>1) dl->AddLine(pts[validCount-1], pts[0], strokeCol, 2.5f);
    }
    g_soundPings.erase(std::remove_if(g_soundPings.begin(),g_soundPings.end(),[](const SoundPing& p){return p.lifetime<=0.f;}),g_soundPings.end());
}

static void DrawLogs(float dt,float sw,float sh){
    if(g_logs.empty()) return;
    ImDrawList*dl=ImGui::GetForegroundDrawList();if(!dl)return;
    ImFont* mainFont=font::bold?font::bold:ImGui::GetFont();
    ImFont* iconFont=font::icomoon;
    float cx=sw*0.5f,cy=sh*0.5f,y=cy+28.f;
    static const char hitIcon[] = "\xee\x80\x81";
    static const char killIcon[] = "\xee\x80\x82";
    for(auto& l: g_logs){
        l.lifetime-=dt;
        float a=Clampf(l.lifetime/l.maxlife,0.f,1.f);
        ImU32 col=IM_COL32((l.color>>IM_COL32_R_SHIFT)&0xFF,(l.color>>IM_COL32_G_SHIFT)&0xFF,(l.color>>IM_COL32_B_SHIFT)&0xFF,(int)(255*a));
        ImVec2 ts=mainFont->CalcTextSizeA(14.f,FLT_MAX,0.f,l.text);
        float x=cx-ts.x*0.5f;
        float iconW=0.f;
        if(iconFont){
            const char* icon=(l.type==1)?killIcon:hitIcon;
            ImVec2 is=iconFont->CalcTextSizeA(14.f,FLT_MAX,0.f,icon);
            iconW=is.x+4.f;
            x-=iconW*0.5f;
            dl->AddText(iconFont,14.f,{x+1.f,y+1.f},IM_COL32(0,0,0,(int)(180*a)),icon);
            dl->AddText(iconFont,14.f,{x,y},col,icon);
        }
        x+=iconW;
        dl->AddText(mainFont,14.f,{x+1.f,y+1.f},IM_COL32(0,0,0,(int)(180*a)),l.text);
        dl->AddText(mainFont,14.f,{x,y},col,l.text);
        y+=18.f;
    }
    g_logs.erase(std::remove_if(g_logs.begin(),g_logs.end(),[](const LogEntry& l){return l.lifetime<=0.f;}),g_logs.end());
}

static void ClearBombInfo(){
    g_bombActive = false;
    g_bombSite = -1;
    g_bombExplodeTime = 0.f;
    g_bombDefusing = false;
    g_bombDefuseEnd = 0.f;
    g_bombDefuserPawn = 0;
    g_bombPos = {};
}

static uintptr_t FindBombEntity(){
    uintptr_t planted = Rd<uintptr_t>(g_client + offsets::client::dwPlantedC4);
    if(IsLikelyPtr(planted)) return planted;

    uintptr_t entityList = Rd<uintptr_t>(g_client + offsets::client::dwEntityList);
    if(!entityList) return 0;

    for(int i = 1; i < 1024; i++){
        uintptr_t chunk = Rd<uintptr_t>(entityList + 8*((i&0x7FFF)>>9) + 16);
        if(!chunk) continue;
        uintptr_t ent = Rd<uintptr_t>(chunk + 112*(i&0x1FF));
        if(!IsLikelyPtr(ent)) continue;
        if(Rd<bool>(ent + offsets::planted_c4::m_bBombTicking)) return ent;
    }
    return 0;
}

static void UpdateBombInfo(){
    if(!g_bombTimerEnabled||!g_client){
        ClearBombInfo();
        return;
    }

    UINT64 nowMs = GetTickCount64();
    if(nowMs - g_lastBombScanMs < 100) return;
    g_lastBombScanMs = nowMs;

    ClearBombInfo();

    uintptr_t ent = FindBombEntity();
    if(!IsLikelyPtr(ent)) return;

    float blow = Rd<float>(ent + offsets::planted_c4::m_flC4Blow);
    if(blow <= 0.f) return;

    uintptr_t scn = Rd<uintptr_t>(ent + offsets::base_entity::m_pGameSceneNode);
    if(scn) g_bombPos = Rd<Vec3>(scn + offsets::scene_node::m_vecAbsOrigin);

    g_bombActive = Rd<bool>(ent + offsets::planted_c4::m_bBombTicking);
    if(!g_bombActive) return;

    g_bombSite = Rd<int>(ent + offsets::planted_c4::m_nBombSite);
    g_bombExplodeTime = blow;
    g_bombDefusing = Rd<bool>(ent + offsets::planted_c4::m_bBeingDefused);
    if(!g_bombDefusing) return;

    g_bombDefuseEnd = Rd<float>(ent + offsets::planted_c4::m_flDefuseCountDown);
    uintptr_t entityList = Rd<uintptr_t>(g_client + offsets::client::dwEntityList);
    uint32_t hDefuser = Rd<uint32_t>(ent + offsets::planted_c4::m_hBombDefuser);
    g_bombDefuserPawn = (entityList && hDefuser) ? ResolveHandle(entityList, hDefuser) : 0;
}

static void DrawBombTimer(float sw){
    if(!g_bombTimerEnabled||!g_bombActive) return;
    float now = GetCurTime();
    float tLeft = g_bombExplodeTime - now;
    if(tLeft <= 0.f) return;
    if(tLeft > 42.f) return;
    char tail[96];
    if(g_bombDefusing){
        float dLeft = g_bombDefuseEnd - now;
        if(dLeft < 0.f) dLeft = 0.f;
        std::snprintf(tail,sizeof(tail)," | %.1fs | Defuse %.1fs", tLeft, dLeft);
    }else{
        std::snprintf(tail,sizeof(tail)," | %.1fs", tLeft);
    }
    char siteStr[2] = { (char)(g_bombSite==1?'B':(g_bombSite==0?'A':'?')), '\0' };
    ImDrawList*dl=ImGui::GetForegroundDrawList();if(!dl)return;
    ImFont* reg = font::regular ? font::regular : ImGui::GetFont();
    const float fs = reg->LegacySize;
    ImVec2 wBomb = reg->CalcTextSizeA(fs, FLT_MAX, 0.f, "Bomb ");
    ImVec2 wSite = reg->CalcTextSizeA(fs, FLT_MAX, 0.f, siteStr);
    ImVec2 wTail = reg->CalcTextSizeA(fs, FLT_MAX, 0.f, tail);
    float tw = wBomb.x + wSite.x + wTail.x;
    float th = (std::max)({ wBomb.y, wSite.y, wTail.y });
    float pad=8.f;
    ImVec2 pos{sw*0.5f-tw*0.5f-pad,30.f};
    ImVec2 boxMax{pos.x+tw+pad*2.f,pos.y+th+pad*2.f};
    DrawOverlayWatermarkChrome(dl, pos, boxMax, 6.f);
    ImU32 acc = IM_COL32((int)(g_accentColor[0]*255),(int)(g_accentColor[1]*255),(int)(g_accentColor[2]*255),255);
    ImU32 dim = IM_COL32(180,182,190,255);
    float tx = pos.x + pad;
    float ty = pos.y + pad;
    dl->AddText(reg, fs, {tx, ty}, dim, "Bomb ");
    tx += wBomb.x;
    dl->AddText(reg, fs, {tx, ty}, acc, siteStr);
    tx += wSite.x;
    dl->AddText(reg, fs, {tx, ty}, dim, tail);
}
