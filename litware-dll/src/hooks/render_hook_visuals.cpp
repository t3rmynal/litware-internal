// визуал

static constexpr int MAX_PARTICLES = 1500;

static void UpdateAndDrawParticles(float dt,float sw,float sh){
    ImDrawList*dl=ImGui::GetBackgroundDrawList();if(!dl)return;
    const float* vm = g_client ? reinterpret_cast<const float*>(g_client+offsets::client::dwViewMatrix) : nullptr;
    bool worldReady = HasUsableLocalWorld();
    if(!worldReady){
        g_pendingKillParticles = false;
        g_particles.erase(std::remove_if(g_particles.begin(), g_particles.end(), [](const Particle& p){
            return p.is3D;
        }), g_particles.end());
    }else if(!g_particlesWorld){
        g_particles.erase(std::remove_if(g_particles.begin(), g_particles.end(), [](const Particle& p){
            return p.is3D && p.type <= 3;
        }), g_particles.end());
    }
    if(g_pendingKillParticles && vm && g_killEffectEnabled){
        ImU32 accentCol=IM_COL32((int)(g_accentColor[0]*255),(int)(g_accentColor[1]*255),(int)(g_accentColor[2]*255),230);
        for(int i=0;i<67 && (int)g_particles.size()<MAX_PARTICLES;i++){
            Particle p{}; p.type=4; p.color=accentCol;
            p.worldPos=g_lastKillEffectPos; p.is3D=true;
            float theta=Randf(0.f,6.2831853f); float phi=Randf(0.2f,1.57f);
            float spd=Randf(80.f,180.f); float vx=spd*sinf(phi)*cosf(theta);
            float vy=spd*sinf(phi)*sinf(theta); float vz=Randf(40.f,120.f);
            p.worldVel={vx,vy,vz}; p.size=Randf(1.5f,4.f); p.maxlife=Randf(1.2f,2.5f);
            p.lifetime=p.maxlife; p.phase=Randf(0.f,6.28f); p.rot=Randf(0.f,6.28f); p.spin=Randf(-2.f,2.f);
            g_particles.push_back(p);
        }
        g_pendingKillParticles=false;
    }
    static float snowAcc=0.f,sakuraAcc=0.f,starAcc=0.f;
    int snowRate=(g_snowDensity==0?25:(g_snowDensity==1?60:120));
    if(g_snowEnabled){snowAcc+=dt*(float)snowRate;}
    if(g_sakuraEnabled){sakuraAcc+=dt*40.f;}
    if(g_starsEnabled){starAcc+=dt*12.f;}
    bool use3D = worldReady && (vm != nullptr) && g_particlesWorld && (g_esp_count > 0 || (g_localOrigin.x*g_localOrigin.x + g_localOrigin.y*g_localOrigin.y + g_localOrigin.z*g_localOrigin.z) > 100.f);
    float worldRadius = g_particlesWorldRadius;
    float worldHeight = g_particlesWorldHeight;
    float worldFloor = g_particlesWorldFloor;
    float wind = g_particlesWind;
    Vec3 windVec{wind, wind*0.35f, 0.f};

    auto spawnWorld=[&](Particle& p){
        float ang = Randf(0.f, 6.2831853f);
        float rad = sqrtf(Randf(0.f, 1.f)) * worldRadius;
        float ox = cosf(ang) * rad;
        float oy = sinf(ang) * rad;
        p.worldPos = {g_localOrigin.x + ox, g_localOrigin.y + oy, g_localOrigin.z + worldFloor + Randf(0.f, worldHeight)};
        p.rot = Randf(0.f, 6.2831853f);
        p.phase = Randf(0.f, 6.2831853f);
        if(p.type==2){
            p.worldVel = {0.f,0.f,0.f};
        }else if(p.type==1){
            p.worldVel = {Randf(-8.f,8.f), Randf(-8.f,8.f), Randf(-45.f,-15.f)};
        }else if(p.type==3){
            p.worldVel = {0.f,0.f,0.f};
        }else{
            p.worldVel = {Randf(-5.f,5.f), Randf(-5.f,5.f), Randf(-70.f,-30.f)};
        }
    };

    auto spawn=[&](int count,int type){
        for(int i=0;i<count;i++){
            if((int)g_particles.size() >= MAX_PARTICLES) break;
            Particle p{};
            p.size=Randf(1.5f,3.5f);
            p.maxlife=Randf(4.f,9.f);
            p.lifetime=p.maxlife;
            p.phase=Randf(0.f,6.28f);
            p.rot=Randf(0.f,6.28f);
            p.spin=Randf(-1.5f,1.5f);
            p.type=type;
            if(type==0)p.color=IM_COL32(255,255,255,200);
            else if(type==1)p.color=IM_COL32((int)(g_sakuraCol[0]*255),(int)(g_sakuraCol[1]*255),(int)(g_sakuraCol[2]*255),(int)(g_sakuraCol[3]*255));
            else if(type==3)p.color=IM_COL32((int)(g_accentColor[0]*255),(int)(g_accentColor[1]*255),(int)(g_accentColor[2]*255),200);
            else p.color=IM_COL32(210,210,230,200);
            if(use3D){
                p.is3D=true;
                spawnWorld(p);
            }else{
                p.is3D=false;
                p.x=Randf(0.f,sw);
                p.y=Randf(-20.f,0.f);
                p.vx=Randf(-10.f,10.f);
                p.vy=Randf(40.f,120.f);
            }
            g_particles.push_back(p);
        }
    };
    while(snowAcc>=1.f){spawn(1,0);snowAcc-=1.f;}
    while(sakuraAcc>=1.f){spawn(1,1);sakuraAcc-=1.f;}
    while(starAcc>=1.f){spawn(1,2);starAcc-=1.f;}
    for(auto& p: g_particles){
        p.lifetime-=dt;
        if(p.is3D){
            p.phase+=dt;
            p.rot += p.spin * dt;
            float sway = (p.type==1 ? 12.f : 6.f) * sinf(p.phase + p.rot);
            p.worldPos.x += (p.worldVel.x + windVec.x + sway) * dt;
            p.worldPos.y += (p.worldVel.y + windVec.y + (p.type==1 ? -sway * 0.4f : 0.f)) * dt;
            p.worldPos.z += p.worldVel.z * dt;
            if(p.type==0){p.worldVel.z -= 8.f * dt;}
            if(p.type==1){p.worldVel.z -= 5.f * dt;}
            if(p.type==2 || p.type==3){}
            if(p.type==4){p.worldVel.z -= 12.f * dt;}
            if(p.type==5){
                p.worldVel.z -= 540.f * dt;
                p.worldVel.x *= (1.f - 1.4f * dt);
                p.worldVel.y *= (1.f - 1.4f * dt);
            }

            float baseZ = g_localOrigin.z + worldFloor;
            float topZ = baseZ + worldHeight;
            if(p.type==4){
                if(p.worldPos.z < baseZ) p.lifetime = 0.f;
            }else if(p.type==5){
                if(p.worldPos.z < baseZ - 10.f) p.lifetime = 0.f;
            }else if(p.worldPos.z < baseZ){
                p.worldPos.z = topZ + Randf(0.f, worldHeight*0.15f);
                p.worldPos.x = g_localOrigin.x;
                p.worldPos.y = g_localOrigin.y;
                spawnWorld(p);
            }
            if(p.type!=4 && p.type!=5){
            float dx = p.worldPos.x - g_localOrigin.x;
            float dy = p.worldPos.y - g_localOrigin.y;
            float dist2 = dx*dx + dy*dy;
            float maxR = worldRadius * 1.35f;
            if(dist2 > maxR*maxR){
                spawnWorld(p);
            }
            }
        }else{
            p.x+=p.vx*dt;p.y+=p.vy*dt;
            if(p.type==1){p.vx+=sinf(p.phase+p.lifetime)*5.f*dt;}
            if(p.type==2){p.phase+=dt;}
        }
    }
    g_particles.erase(std::remove_if(g_particles.begin(),g_particles.end(),[&](const Particle& p){
        if(p.lifetime<=0.f) return true;
        if(!p.is3D && (p.x<-20.f||p.x>sw+20.f||p.y>sh+20.f)) return true;
        return false;
    }),g_particles.end());
    for(const auto& p: g_particles){
        float x=p.x,y=p.y;
        if(p.is3D){
            if(!vm) continue;
            if(!WorldToScreen(vm,p.worldPos,g_esp_screen_w,g_esp_screen_h,x,y)) continue;
        }
        float life = Clampf(p.lifetime/p.maxlife,0.f,1.f);
        float depth = 1.f;
        if(p.is3D){
            Vec3 d{p.worldPos.x - g_localOrigin.x, p.worldPos.y - g_localOrigin.y, p.worldPos.z - g_localOrigin.z};
            float dist = d.length();
            depth = 1.f / (1.f + dist * g_particlesDepthFade);
            depth = Clampf(depth, 0.25f, 1.2f);
        }
        float alpha = Clampf(life * depth, 0.f, 1.f);
        float size = p.size * LerpF(0.65f, 1.4f, depth);
        if(p.type==0){
            ImU32 soft = WithAlpha(p.color, alpha*0.35f);
            ImU32 core = WithAlpha(p.color, alpha);
            dl->AddCircleFilled({x,y},size*1.6f,soft,12);
            dl->AddCircleFilled({x,y},size,core,12);
        }else if(p.type==1){
            ImU32 petal = WithAlpha(p.color, alpha*0.9f);
            DrawRotatedQuad(dl, {x,y}, size*2.2f, size*1.2f, p.rot, petal);
            DrawRotatedQuad(dl, {x,y}, size*1.4f, size*0.8f, p.rot + 0.8f, WithAlpha(p.color, alpha*0.55f));
        }else if(p.type==4){
            ImU32 soft = WithAlpha(p.color, alpha*0.4f);
            ImU32 core = WithAlpha(p.color, alpha);
            dl->AddCircleFilled({x,y},size*1.4f,soft,12);
            dl->AddCircleFilled({x,y},size,core,12);
        }else if(p.type==5){
            ImU32 soft = WithAlpha(p.color, alpha*0.55f);
            ImU32 core = WithAlpha(p.color, alpha);
            dl->AddCircleFilled({x,y}, size * 2.0f, soft, 10);
            dl->AddCircleFilled({x,y}, size * 0.85f, core, 10);
        }else{
            float tw = 0.4f + 0.6f * (sinf(p.phase*2.f + p.rot)*0.5f + 0.5f);
            ImU32 col = WithAlpha(p.color, alpha * tw);
            dl->AddCircleFilled({x,y},size*0.7f,col,10);
            dl->AddLine({x-size,y},{x+size,y},WithAlpha(p.color, alpha*0.35f),1.f);
            dl->AddLine({x,y-size},{x,y+size},WithAlpha(p.color, alpha*0.35f),1.f);
        }
    }
}

static void DrawDamageFloaters(float sw, float sh){
    if(g_damageFloaters.empty()) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if(!dl) return;
    ImFont* fb = font::bold ? font::bold : ImGui::GetFont();
    const float* vm = g_client ? reinterpret_cast<const float*>(g_client+offsets::client::dwViewMatrix) : nullptr;
    UINT64 now = GetTickCount64();

    for(auto it = g_damageFloaters.begin(); it != g_damageFloaters.end(); ){
        float elapsed = (float)(now - it->spawnMs) / 1000.f;
        if(elapsed >= it->duration){
            it = g_damageFloaters.erase(it);
            continue;
        }
        float u = elapsed / it->duration;

        float worldZOff = u * 55.f * g_damageFloaterScale;
        Vec3 worldPos{it->wx + it->randOffX, it->wy, it->wz + worldZOff};

        float sx, sy;
        bool onScreen = vm && WorldToScreen(vm, worldPos, sw, sh, sx, sy);
        if(!onScreen){ ++it; continue; }

        float entryT = Clampf(elapsed / 0.12f, 0.f, 1.f);
        float entryScale = 0.55f + 0.45f * (entryT * entryT * (3.f - 2.f * entryT));
        float fadeT = Clampf((u - 0.65f) / 0.35f, 0.f, 1.f);
        float alpha = 1.f - fadeT * fadeT;

        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d", it->damage);

        float baseSize = Clampf(14.f + (float)it->damage * 0.08f, 14.f, 24.f) * g_damageFloaterScale;
        float fs = baseSize * entryScale;
        ImVec2 ts = fb->CalcTextSizeA(fs, FLT_MAX, 0.f, buf);

        float tx = sx - ts.x * 0.5f;
        float ty = sy - ts.y * 0.5f;

        ImU32 textCol;
        if(it->damage >= 100)      textCol = IM_COL32(255,80,80,(int)(255*alpha));
        else if(it->damage >= 90)  textCol = IM_COL32(255,210,50,(int)(255*alpha));
        else                       textCol = IM_COL32(255,255,255,(int)(255*alpha));

        for(int sh2=2;sh2>=1;sh2--){
            float o=(float)sh2;
            dl->AddText(fb, fs, {tx+o,ty+o}, IM_COL32(0,0,0,(int)(190*alpha)), buf);
        }
        dl->AddText(fb, fs, {tx, ty}, textCol, buf);
        ++it;
    }
}

static void DrawKillEffect(float sw, float sh){
    if(!g_killEffectEnabled || !g_lastKillEffectTime) return;
    UINT64 elapsed = GetTickCount64() - g_lastKillEffectTime;
    float durMs = g_killEffectDuration * 1000.f;
    if(elapsed >= (UINT64)durMs) { g_lastKillEffectTime = 0; return; }
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if(!dl) return;
    float t = 1.f - (float)elapsed / durMs;
    int alpha = (int)(80 * t);
    dl->AddRectFilled({0,0},{sw,sh}, IM_COL32(180,60,80,alpha));
}

static void DrawWatermark(float sw){
    if(!g_watermarkEnabled){
        g_watermarkOverlayHeight = 0.f;
        return;
    }
    ImDrawList* dl = ImGui::GetForegroundDrawList(); if(!dl) return;
    ImFont* fBold = font::bold ? font::bold : ImGui::GetFont();
    ImFont* fReg  = font::regular ? font::regular : ImGui::GetFont();
    ImGuiIO& io   = ImGui::GetIO();

    SYSTEMTIME st{}; GetLocalTime(&st);
    char timeBuf[16]; std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", st.wHour, st.wMinute);
    const float fps    = io.Framerate;
    const char* label = "litware";
    char fpsBuf[16]; std::snprintf(fpsBuf, sizeof(fpsBuf), "%.0f", fps);
    const char* pingBuf = "ping 0";

    const ImU32 colAccent = IM_COL32(220,220,220,255);
    const ImU32 colText = IM_COL32(220,220,220,255);
    const ImU32 colDim = IM_COL32(140,140,140,255);
    std::vector<OverlayBarItem> items{
        {label, colAccent, true},
    };
    if(g_showFpsWatermark) items.push_back({fpsBuf, colText, false});
    items.push_back({timeBuf, colDim, false});
    items.push_back({pingBuf, colDim, false});
    ImVec2 size = DrawOverlayWatermarkBar(dl, fBold, fReg, sw - 12.f, 12.f, items, true, 170.f);
    g_watermarkOverlayHeight = size.y;
}

static void DrawNoCrosshair(float sw,float sh){
    if(!g_noCrosshair) return;
    ImDrawList*dl=ImGui::GetForegroundDrawList();if(!dl)return;
    float cx=sw*0.5f, cy=sh*0.5f;
    dl->AddCircleFilled({cx,cy},20.f,IM_COL32(0,0,0,255),24);
}

static void DrawFovCircle(float sw, float sh){
    if(!g_fovCircleEnabled||!g_aimbotEnabled) return;
    ImDrawList*dl=ImGui::GetForegroundDrawList();if(!dl)return;
    float cx=sw*0.5f, cy=sh*0.5f;
    float fovRad = g_aimbotFov * (3.14159265f/180.f);
    float radius = (std::min)(sw,sh) * 0.5f * tanf(fovRad);
    if(radius < 5.f) radius = 5.f;
    if(radius > 400.f) radius = 400.f;
    ImU32 col = IM_COL32((int)(g_fovCircleCol[0]*255),(int)(g_fovCircleCol[1]*255),(int)(g_fovCircleCol[2]*255),(int)(g_fovCircleCol[3]*255));
    dl->AddCircle({cx,cy}, radius, col, 64, 1.2f);
}

static ImU32 HealthCol(int hp){
    float t=Clampf(hp/100.f,0.f,1.f);
    return IM_COL32((int)(255*(1.f-t)),(int)(210*t),(int)(50*t),255);
}

static void DrawCornerBox(ImDrawList*dl,float l,float t,float r,float b,ImU32 col,float thick){
    float lx=(r-l)*0.22f,ly=(b-t)*0.22f;
    dl->AddLine({l,t},{l+lx,t},col,thick);dl->AddLine({l,t},{l,t+ly},col,thick);
    dl->AddLine({r,t},{r-lx,t},col,thick);dl->AddLine({r,t},{r,t+ly},col,thick);
    dl->AddLine({l,b},{l+lx,b},col,thick);dl->AddLine({l,b},{l,b-ly},col,thick);
    dl->AddLine({r,b},{r-lx,b},col,thick);dl->AddLine({r,b},{r,b-ly},col,thick);
}
static bool GetOofArrowPos(const float* vm, const Vec3& head, int sw, int sh, float& ox, float& oy){
    float w = vm[12]*head.x + vm[13]*head.y + vm[14]*head.z + vm[15];
    if(w > -0.001f && w < 0.001f) return false;
    float invW = 1.f / (w < 0.001f ? 0.001f : w);
    float x = (vm[0]*head.x + vm[1]*head.y + vm[2]*head.z + vm[3]) * invW;
    float y = (vm[4]*head.x + vm[5]*head.y + vm[6]*head.z + vm[7]) * invW;
    float sx = (sw*0.5f) + (x * sw*0.5f);
    float sy = (sh*0.5f) - (y * sh*0.5f);
    float cx = sw*0.5f, cy = sh*0.5f;
    float dx = sx - cx, dy = sy - cy;
    if(fabsf(dx) < 0.001f && fabsf(dy) < 0.001f) return false;
    float t = 1e9f;
    float margin = 30.f;
    if(dx > 0.001f) t = (sw - margin - cx) / dx; else if(dx < -0.001f) t = (margin - cx) / dx;
    if(dy > 0.001f) { float ty = (sh - margin - cy) / dy; if(ty < t) t = ty; }
    else if(dy < -0.001f) { float ty = (margin - cy) / dy; if(ty < t) t = ty; }
    if(t < 0) t = 0;
    ox = cx + dx * t;
    oy = cy + dy * t;
    return true;
}

static void DrawCoalBox(ImDrawList*dl,float l,float t,float r,float b,ImU32 col,float thick){
    float lx=(r-l)*0.25f,ly=(b-t)*0.25f;
    dl->AddLine({l,t},{l+lx,t},col,thick);dl->AddLine({l,t},{l,t+ly},col,thick);
    dl->AddLine({r,t},{r-lx,t},col,thick);dl->AddLine({r,t},{r,t+ly},col,thick);
    dl->AddLine({l,b},{l+lx,b},col,thick);dl->AddLine({l,b},{l,b-ly},col,thick);
    dl->AddLine({r,b},{r-lx,b},col,thick);dl->AddLine({r,b},{r,b-ly},col,thick);
}
static void DrawOutlineBox(ImDrawList*dl,float l,float t,float r,float b,ImU32 col,float thick){
    dl->AddRect({l-1.f,t-1.f},{r+1.f,b+1.f},IM_COL32(0,0,0,(int)(235)),0.f,0,thick+1.f);
    dl->AddRect({l+1.f,t+1.f},{r-1.f,b-1.f},IM_COL32(0,0,0,(int)(130)),0.f,0,thick);
    dl->AddRect({l,t},{r,b},col,0.f,0,thick);
}
static void DrawOutlineCoalBox(ImDrawList*dl,float l,float t,float r,float b,ImU32 col,float thick){
    ImU32 black=IM_COL32(0,0,0,255);
    DrawCoalBox(dl,l,t,r,b,black,thick+1.f);
    DrawCoalBox(dl,l+1.f,t+1.f,r-1.f,b-1.f,col,thick);
    DrawCoalBox(dl,l+2.f,t+2.f,r-2.f,b-2.f,black,thick);
}

static void DrawFilledEllipse(ImDrawList* dl, const ImVec2& center, float rx, float ry, ImU32 col, int segments = 24){
    if(!dl || rx <= 0.f || ry <= 0.f) return;
    const int maxSeg = 64;
    int n = segments < 6 ? 6 : (segments > maxSeg ? maxSeg : segments);
    ImVec2 pts[maxSeg];
    float step = 6.2831853f / (float)n;
    for(int i=0;i<n;i++){
        float a = step * i;
        pts[i] = {center.x + cosf(a)*rx, center.y + sinf(a)*ry};
    }
    dl->AddConvexPolyFilled(pts, n, col);
}

static void DrawRotatedQuad(ImDrawList* dl, ImVec2 center, float w, float h, float angle, ImU32 col){
    if(!dl || w <= 0.f || h <= 0.f) return;
    float c = cosf(angle), s = sinf(angle);
    float hw = w * 0.5f, hh = h * 0.5f;
    ImVec2 pts[4] = {
        {center.x + (-hw*c - -hh*s), center.y + (-hw*s + -hh*c)},
        {center.x + ( hw*c - -hh*s), center.y + ( hw*s + -hh*c)},
        {center.x + ( hw*c -  hh*s), center.y + ( hw*s +  hh*c)},
        {center.x + (-hw*c -  hh*s), center.y + (-hw*s +  hh*c)}
    };
    dl->AddConvexPolyFilled(pts, 4, col);
}

static bool DrawSkeletonBones(ImDrawList*dl,const ESPEntry& e,ImU32 col,ImU32 shadowCol=0){
    if(!dl||!g_client||!e.pawn) return false;
    UpdatePawnBones(e.pawn);
    const float* vm = reinterpret_cast<const float*>(g_client + offsets::client::dwViewMatrix);
    if(!vm) return false;
    Vec3 head{}, neck{}, spine1{}, spine2{}, spine3{}, pelvis{};
    Vec3 armUpL{}, armLoL{}, handL{}, armUpR{}, armLoR{}, handR{};
    Vec3 legUpL{}, legLoL{}, ankleL{}, legUpR{}, legLoR{}, ankleR{};
    bool hHead=GetBonePos(e.pawn,BONE_HEAD,head);
    bool hNeck=GetBonePos(e.pawn,BONE_NECK,neck);
    bool hSp1=GetBonePos(e.pawn,BONE_SPINE1,spine1);
    bool hSp2=GetBonePos(e.pawn,BONE_SPINE2,spine2);
    bool hSp3=GetBonePos(e.pawn,BONE_SPINE3,spine3);
    bool hPel=GetBonePos(e.pawn,BONE_PELVIS,pelvis);
    bool hArmUL=GetBonePos(e.pawn,BONE_ARM_UP_L,armUpL);
    bool hArmLL=GetBonePos(e.pawn,BONE_ARM_LO_L,armLoL);
    bool hHandL=GetBonePos(e.pawn,BONE_HAND_L,handL);
    bool hArmUR=GetBonePos(e.pawn,BONE_ARM_UP_R,armUpR);
    bool hArmLR=GetBonePos(e.pawn,BONE_ARM_LO_R,armLoR);
    bool hHandR=GetBonePos(e.pawn,BONE_HAND_R,handR);
    bool hLegUL=GetBonePos(e.pawn,BONE_LEG_UP_L,legUpL);
    bool hLegLL=GetBonePos(e.pawn,BONE_LEG_LO_L,legLoL);
    bool hAnkL=GetBonePos(e.pawn,BONE_ANKLE_L,ankleL);
    bool hLegUR=GetBonePos(e.pawn,BONE_LEG_UP_R,legUpR);
    bool hLegLR=GetBonePos(e.pawn,BONE_LEG_LO_R,legLoR);
    bool hAnkR=GetBonePos(e.pawn,BONE_ANKLE_R,ankleR);
    auto validPos = [](const Vec3& v){
        if(!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return false;
        if(fabsf(v.x) > 100000.f || fabsf(v.y) > 100000.f || fabsf(v.z) > 100000.f) return false;
        if(fabsf(v.x) < 0.001f && fabsf(v.y) < 0.001f && fabsf(v.z) < 0.001f) return false;
        return true;
    };
    hHead = hHead && validPos(head);
    hNeck = hNeck && validPos(neck);
    hSp1 = hSp1 && validPos(spine1);
    hSp2 = hSp2 && validPos(spine2);
    hSp3 = hSp3 && validPos(spine3);
    hPel = hPel && validPos(pelvis);
    hArmUL = hArmUL && validPos(armUpL);
    hArmLL = hArmLL && validPos(armLoL);
    hHandL = hHandL && validPos(handL);
    hArmUR = hArmUR && validPos(armUpR);
    hArmLR = hArmLR && validPos(armLoR);
    hHandR = hHandR && validPos(handR);
    hLegUL = hLegUL && validPos(legUpL);
    hLegLL = hLegLL && validPos(legLoL);
    hAnkL = hAnkL && validPos(ankleL);
    hLegUR = hLegUR && validPos(legUpR);
    hLegLR = hLegLR && validPos(legLoR);
    hAnkR = hAnkR && validPos(ankleR);
    if(!(hHead && hNeck && hPel)) return false;
    bool drew=false;
    float thick = Clampf(g_skeletonThick, 0.5f, 3.5f) * Clampf(g_espScale, 0.7f, 1.5f);
    const float shadowOff = (shadowCol != 0) ? 1.5f : 0.f;
    auto line=[&](bool ha,const Vec3& a,bool hb,const Vec3& b){
        if(!ha||!hb) return;
        float ax,ay,bx,by;
        if(WorldToScreen(vm,a,g_esp_screen_w,g_esp_screen_h,ax,ay)&&WorldToScreen(vm,b,g_esp_screen_w,g_esp_screen_h,bx,by)){
            if(!std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(bx) || !std::isfinite(by)) return;
            if(shadowCol!=0){ dl->AddLine({ax+shadowOff,ay+shadowOff},{bx+shadowOff,by+shadowOff},shadowCol,thick+0.5f); }
            dl->AddLine({ax,ay},{bx,by},col,thick);
            drew=true;
        }
    };
    line(hHead,head,hNeck,neck);
    line(hNeck,neck,hSp3,spine3);
    line(hSp3,spine3,hSp2,spine2);
    line(hSp2,spine2,hSp1,spine1);
    line(hSp1,spine1,hPel,pelvis);
    line(hSp3,spine3,hArmUL,armUpL);
    line(hArmUL,armUpL,hArmLL,armLoL);
    line(hArmLL,armLoL,hHandL,handL);
    line(hSp3,spine3,hArmUR,armUpR);
    line(hArmUR,armUpR,hArmLR,armLoR);
    line(hArmLR,armLoR,hHandR,handR);
    line(hPel,pelvis,hLegUL,legUpL);
    line(hLegUL,legUpL,hLegLL,legLoL);
    line(hLegLL,legLoL,hAnkL,ankleL);
    line(hPel,pelvis,hLegUR,legUpR);
    line(hLegUR,legUpR,hLegLR,legLoR);
    line(hLegLR,legLoR,hAnkR,ankleR);
    return drew;
}

static void DrawESP(){
    if(!g_espEnabled)return;
    ImDrawList*dl=ImGui::GetForegroundDrawList();if(!dl)return;
    const float* vm = g_client ? reinterpret_cast<const float*>(g_client+offsets::client::dwViewMatrix) : nullptr;
    uintptr_t entityList = g_client ? Rd<uintptr_t>(g_client+offsets::client::dwEntityList) : 0;
    ImFont* espFont = GetEspFont();
    auto drawOne=[&](const ESPEntry& e, float alphaMul){
        if(!e.valid||e.distance>g_espMaxDist)return;
        bool enemy=(e.team!=g_esp_local_team);float*ecol=enemy?g_espEnemyCol:g_espTeamCol;
        if(!enemy && !g_espShowTeam) return;
        float s = Clampf(g_espScale, 0.7f, 1.5f);
        float boxThick = g_espBoxThick * s;
        float alpha=(e.visible?1.f:0.5f)*alphaMul;float bl=e.box_l,bt2=e.box_t,br=e.box_r,bb=e.box_b;
        float bw=br-bl,bh=bb-bt2,cx=(bl+br)*0.5f;
        ImU32 boxCol=IM_COL32((int)(ecol[0]*255),(int)(ecol[1]*255),(int)(ecol[2]*255),(int)(alpha*255));
        ImU32 dimCol=IM_COL32(160,160,170,(int)(180*alpha));
        float belowY = bb + 3.f * s;
        const float boxRnd = 3.5f * s;
        if(g_espDrawBox){
            if(g_espBoxShadow){
                dl->AddRectFilled({bl+4.f,bt2+4.f},{br+4.f,bb+4.f},IM_COL32(0,0,0,(int)(34*alpha)),boxRnd+1.5f);
                dl->AddRectFilled({bl+2.f,bt2+2.f},{br+2.f,bb+2.f},IM_COL32(0,0,0,(int)(48*alpha)),boxRnd+0.5f);
            }
            for(int g=3;g>=1;g--){
                int r=(boxCol>>IM_COL32_R_SHIFT)&0xFF,g_=(boxCol>>IM_COL32_G_SHIFT)&0xFF,b=(boxCol>>IM_COL32_B_SHIFT)&0xFF;
                int ga=(g==3)?30:(g==2)?16:7;
                dl->AddRect({bl-(float)g,bt2-(float)g},{br+(float)g,bb+(float)g},IM_COL32(r,g_,b,(int)(ga*alpha)),0.f,0,1.15f);
            }
            if(g_espBoxStyle==0){
                DrawCornerBox(dl,bl,bt2,br,bb,IM_COL32(0,0,0,(int)(200*alpha)),boxThick+1.0f);
                DrawCornerBox(dl,bl,bt2,br,bb,boxCol,boxThick);
            }
            else if(g_espBoxStyle==1){
                dl->AddRect({bl,bt2},{br,bb},IM_COL32(0,0,0,(int)(200*alpha)),boxRnd,0,boxThick+1.0f);
                dl->AddRect({bl,bt2},{br,bb},boxCol,boxRnd,0,boxThick);
            }
            else if(g_espBoxStyle==2){
                dl->AddRectFilled({bl,bt2},{br,bb},IM_COL32((int)(ecol[0]*255),(int)(ecol[1]*255),(int)(ecol[2]*255),(int)(42*alpha)),boxRnd);
                DrawCornerBox(dl,bl,bt2,br,bb,IM_COL32(0,0,0,(int)(200*alpha)),boxThick+1.0f);
                DrawCornerBox(dl,bl,bt2,br,bb,boxCol,boxThick);
            }
            else if(g_espBoxStyle==3){
                DrawOutlineBox(dl,bl,bt2,br,bb,boxCol,boxThick);
            }
            else if(g_espBoxStyle==4){
                DrawCoalBox(dl,bl,bt2,br,bb,IM_COL32(0,0,0,(int)(200*alpha)),boxThick+1.0f);
                DrawCoalBox(dl,bl,bt2,br,bb,boxCol,boxThick);
            }
            else if(g_espBoxStyle==5){
                DrawOutlineCoalBox(dl,bl,bt2,br,bb,boxCol,boxThick);
            }
        }
        if(g_espHealth&&e.health>0){
            float fill=Clampf((float)e.health/100.f,0.f,1.f),barW=5.f*s,barOff=8.f*s,barRound=4.f*s;
            ImU32 hbCol=HealthCol(e.health);
            if(g_espHealthStyle==1)hbCol=IM_COL32(60,200,120,(int)(220*alpha));
            if(g_espHealthStyle==2)hbCol=boxCol;
            ImU32 c1=IM_COL32((int)(g_espHealthGradientCol1[0]*255),(int)(g_espHealthGradientCol1[1]*255),(int)(g_espHealthGradientCol1[2]*255),(int)(240*alpha));
            ImU32 c2=IM_COL32((int)(g_espHealthGradientCol2[0]*255),(int)(g_espHealthGradientCol2[1]*255),(int)(g_espHealthGradientCol2[2]*255),(int)(240*alpha));
            ImU32 cFill = LerpColor(c2, c1, fill);
            ImU32 bgDark=IM_COL32(8,8,12,(int)(220*alpha));
            ImU32 borderCol=IM_COL32(40,40,50,(int)(180*alpha));
            float bx=0.f, byBar=0.f;
            bool useGradientGlow=(g_espHealthStyle==0);
            if(g_espHealthPos==0){
                bx=bl-barOff-barW;
                float fillTop = bt2+bh*(1.f-fill);
                for(int g=4;g>=1;g--){
                    float o=(float)g; int glowA=(int)(45*alpha/(float)g);
                    if(useGradientGlow){
                        ImU32 c1g=IM_COL32((c1>>IM_COL32_R_SHIFT)&0xFF,(c1>>IM_COL32_G_SHIFT)&0xFF,(c1>>IM_COL32_B_SHIFT)&0xFF,glowA);
                        ImU32 c2g=IM_COL32((c2>>IM_COL32_R_SHIFT)&0xFF,(c2>>IM_COL32_G_SHIFT)&0xFF,(c2>>IM_COL32_B_SHIFT)&0xFF,glowA);
                        dl->AddRectFilledMultiColor({bx-o-1.f,fillTop-o-1.f},{bx+barW+o+1.f,bb+o+1.f}, c1g,c1g, c2g,c2g);
                    }else
                        dl->AddRectFilled({bx-o-1.f,fillTop-o-1.f},{bx+barW+o+1.f,bb+o+1.f},IM_COL32((hbCol>>IM_COL32_R_SHIFT)&0xFF,(hbCol>>IM_COL32_G_SHIFT)&0xFF,(hbCol>>IM_COL32_B_SHIFT)&0xFF,glowA),barRound+o);
                }
                dl->AddRectFilled({bx-1.f,bt2-1.f},{bx+barW+1.f,bb+1.f},borderCol,barRound+1.f);
                dl->AddRectFilled({bx,bt2},{bx+barW,bb},bgDark,barRound);
                if(g_espHealthStyle==0){
                    dl->AddRectFilledMultiColor({bx,fillTop},{bx+barW,bb}, cFill,cFill, c2,c2);
                }else{
                    dl->AddRectFilled({bx,fillTop},{bx+barW,bb},hbCol,barRound);
                }
                byBar=fillTop;
            }else if(g_espHealthPos==2){
                bx=br+barOff;
                float fillTop = bt2+bh*(1.f-fill);
                for(int g=4;g>=1;g--){
                    float o=(float)g; int glowA=(int)(45*alpha/(float)g);
                    if(useGradientGlow){
                        ImU32 c1g=IM_COL32((c1>>IM_COL32_R_SHIFT)&0xFF,(c1>>IM_COL32_G_SHIFT)&0xFF,(c1>>IM_COL32_B_SHIFT)&0xFF,glowA);
                        ImU32 c2g=IM_COL32((c2>>IM_COL32_R_SHIFT)&0xFF,(c2>>IM_COL32_G_SHIFT)&0xFF,(c2>>IM_COL32_B_SHIFT)&0xFF,glowA);
                        dl->AddRectFilledMultiColor({bx-o-1.f,fillTop-o-1.f},{bx+barW+o+1.f,bb+o+1.f}, c1g,c1g, c2g,c2g);
                    }
                    else
                        dl->AddRectFilled({bx-o-1.f,fillTop-o-1.f},{bx+barW+o+1.f,bb+o+1.f},IM_COL32((hbCol>>IM_COL32_R_SHIFT)&0xFF,(hbCol>>IM_COL32_G_SHIFT)&0xFF,(hbCol>>IM_COL32_B_SHIFT)&0xFF,glowA),barRound+o);
                }
                dl->AddRectFilled({bx-1.f,bt2-1.f},{bx+barW+1.f,bb+1.f},borderCol,barRound+1.f);
                dl->AddRectFilled({bx,bt2},{bx+barW,bb},bgDark,barRound);
                if(g_espHealthStyle==0){
                    dl->AddRectFilledMultiColor({bx,fillTop},{bx+barW,bb}, cFill,cFill, c2,c2);
                }else{
                    dl->AddRectFilled({bx,fillTop},{bx+barW,bb},hbCol,barRound);
                }
                byBar=fillTop;
            }
else if(g_espHealthPos==1){
                float by=bt2-barOff-barW;
                for(int g=4;g>=1;g--){
                    float o=(float)g; int glowA=(int)(45*alpha/(float)g);
                    if(useGradientGlow){
                        ImU32 c1g=IM_COL32((c1>>IM_COL32_R_SHIFT)&0xFF,(c1>>IM_COL32_G_SHIFT)&0xFF,(c1>>IM_COL32_B_SHIFT)&0xFF,glowA);
                        ImU32 c2g=IM_COL32((c2>>IM_COL32_R_SHIFT)&0xFF,(c2>>IM_COL32_G_SHIFT)&0xFF,(c2>>IM_COL32_B_SHIFT)&0xFF,glowA);
                        dl->AddRectFilledMultiColor({bl-o-1.f,by-o-1.f},{br+o+1.f,by+barW+o+1.f}, c2g,c1g, c1g,c2g);
                    }else
                        dl->AddRectFilled({bl-o-1.f,by-o-1.f},{br+o+1.f,by+barW+o+1.f},IM_COL32((hbCol>>IM_COL32_R_SHIFT)&0xFF,(hbCol>>IM_COL32_G_SHIFT)&0xFF,(hbCol>>IM_COL32_B_SHIFT)&0xFF,glowA),barRound+o);
                }
                dl->AddRectFilled({bl-1.f,by-1.f},{br+1.f,by+barW+1.f},borderCol,barRound+1.f);
                dl->AddRectFilled({bl,by},{br,by+barW},bgDark,barRound);
                if(g_espHealthStyle==0){
                    ImU32 c1=IM_COL32((int)(g_espHealthGradientCol1[0]*255),(int)(g_espHealthGradientCol1[1]*255),(int)(g_espHealthGradientCol1[2]*255),(int)(240*alpha));
                    ImU32 c2=IM_COL32((int)(g_espHealthGradientCol2[0]*255),(int)(g_espHealthGradientCol2[1]*255),(int)(g_espHealthGradientCol2[2]*255),(int)(240*alpha));
                    dl->AddRectFilledMultiColor({bl,by},{bl+bw*fill,by+barW}, c2,cFill, cFill,c2);
                }else{
                    dl->AddRectFilled({bl,by},{bl+bw*fill,by+barW},hbCol,barRound);
                }
            }
            if(e.health<100){
                char hpBuf[16]; std::snprintf(hpBuf,sizeof(hpBuf),"%d",e.health);
                ImFont* font=espFont;
                float fsz=(g_espHealthPos==1)?(10.f*s):(g_espNameSize*0.85f*s);
                ImVec2 ts=font->CalcTextSizeA(fsz,FLT_MAX,0.f,hpBuf);
                float tx=bx+(g_espHealthPos==0?barW+2.f:(g_espHealthPos==2?-ts.x-2.f:bl+bw*0.5f-ts.x*0.5f));
                float ty=(g_espHealthPos==1)?bt2-barOff-barW-ts.y-1.f:byBar-ts.y*0.5f;
                dl->AddText(font,fsz,{tx+1.f,ty+1.f},IM_COL32(0,0,0,(int)(180*alpha)),hpBuf);
                dl->AddText(font,fsz,{tx,ty},IM_COL32(255,255,255,(int)(220*alpha)),hpBuf);
            }
        }
        if(g_espHeadDot){
            float dotR = bw*0.16f;
            if(dotR < 7.f * s) dotR = 7.f * s;
            for(int g=4;g>=1;g--){
                float o=(float)g; int r_=(boxCol>>IM_COL32_R_SHIFT)&0xFF,g_=(boxCol>>IM_COL32_G_SHIFT)&0xFF,b=(boxCol>>IM_COL32_B_SHIFT)&0xFF;
                dl->AddCircle({e.head_fx,e.head_fy},dotR+o,IM_COL32(r_,g_,b,(int)(35*alpha/(float)g)),16,1.2f);
            }
            dl->AddCircle({e.head_fx,e.head_fy},dotR,IM_COL32(0,0,0,(int)(180*alpha)),16,1.5f);
            dl->AddCircleFilled({e.head_fx,e.head_fy},dotR*0.55f,
                IM_COL32((int)(ecol[0]*255),(int)(ecol[1]*255),(int)(ecol[2]*255),(int)(200*alpha)),12);
            dl->AddCircle({e.head_fx,e.head_fy},dotR,boxCol,16,1.0f);
        }
        if(g_espLines){
            float sx=(float)g_esp_screen_w*0.5f,sh=(float)g_esp_screen_h;
            float sy = (g_espLineAnchor==0) ? 0.f : ((g_espLineAnchor==2) ? sh : sh*0.5f);
            int r=(int)(ecol[0]*255),g_=(int)(ecol[1]*255),b=(int)(ecol[2]*255);
            for(int gl=3;gl>=1;gl--) dl->AddLine({sx+(float)gl,sy+(float)gl},{e.feet_x+(float)gl,e.feet_y+(float)gl},IM_COL32(0,0,0,(int)(48*alpha/(float)gl)),(1.35f+(float)gl*0.32f)*s);
            dl->AddLine({sx,sy},{e.feet_x,e.feet_y},IM_COL32(r,g_,b,(int)(150*alpha)),1.05f*s);
            dl->AddLine({sx,sy},{e.feet_x,e.feet_y},IM_COL32(255,255,255,(int)(36*alpha)),0.5f*s);
        }
        if(g_espSkeleton){
            ImU32 scol=IM_COL32((int)(ecol[0]*255),(int)(ecol[1]*255),(int)(ecol[2]*255),(int)(180*alpha));
            ImU32 sShadow=IM_COL32(0,0,0,(int)(120*alpha));
            bool drew = DrawSkeletonBones(dl, e, scol, sShadow);
            if(!drew){
                float top=e.box_t, bottom=e.box_b, center=cx;
                float h=bottom-top;
                float shoulderY=top+h*0.28f;
                float pelvisY=top+h*0.55f;
                float neckY=top+h*0.18f;
                float hipY=top+h*0.65f;
                float kneeY=top+h*0.82f;
                float halfShoulder=bw*0.35f;
                float halfHip=bw*0.25f;
                ImVec2 head{e.head_x,e.head_y};
                ImVec2 neck{center,neckY};
                ImVec2 pelvis{center,pelvisY};
                ImVec2 lShoulder{center-halfShoulder,shoulderY};
                ImVec2 rShoulder{center+halfShoulder,shoulderY};
                ImVec2 lElbow{center-halfShoulder*1.3f,shoulderY+h*0.12f};
                ImVec2 rElbow{center+halfShoulder*1.3f,shoulderY+h*0.12f};
                ImVec2 lHand{center-halfShoulder*1.4f,shoulderY+h*0.24f};
                ImVec2 rHand{center+halfShoulder*1.4f,shoulderY+h*0.24f};
                ImVec2 lHip{center-halfHip,hipY};
                ImVec2 rHip{center+halfHip,hipY};
                ImVec2 lKnee{center-halfHip*0.8f,kneeY};
                ImVec2 rKnee{center+halfHip*0.8f,kneeY};
                ImVec2 lFoot{center-halfHip*0.7f,bottom};
                ImVec2 rFoot{center+halfHip*0.7f,bottom};
                ImU32 scolShadow=IM_COL32(0,0,0,(int)(90*alpha));
                auto skLine=[&](ImVec2 a, ImVec2 b){ for(int s=2;s>=1;s--) dl->AddLine({a.x+(float)s,a.y+(float)s},{b.x+(float)s,b.y+(float)s},scolShadow,1.8f+(float)s*0.4f); dl->AddLine(a,b,scol,1.0f); };
                skLine(head, neck); skLine(neck, pelvis); skLine(neck, lShoulder); skLine(neck, rShoulder);
                skLine(lShoulder, lElbow); skLine(rShoulder, rElbow); skLine(lElbow, lHand); skLine(rElbow, rHand);
                skLine(pelvis, lHip); skLine(pelvis, rHip); skLine(lHip, lKnee); skLine(rHip, rKnee); skLine(lKnee, lFoot); skLine(rKnee, rFoot);
                dl->AddLine(neck, lShoulder, scol, 1.0f);
                dl->AddLine(neck, rShoulder, scol, 1.0f);
                dl->AddLine(lShoulder, lElbow, scol, 1.0f);
                dl->AddLine(rShoulder, rElbow, scol, 1.0f);
                dl->AddLine(lElbow, lHand, scol, 1.0f);
                dl->AddLine(rElbow, rHand, scol, 1.0f);
                dl->AddLine(pelvis, lHip, scol, 1.0f);
                dl->AddLine(pelvis, rHip, scol, 1.0f);
                dl->AddLine(lHip, lKnee, scol, 1.0f);
                dl->AddLine(rHip, rKnee, scol, 1.0f);
                dl->AddLine(lKnee, lFoot, scol, 1.0f);
                dl->AddLine(rKnee, rFoot, scol, 1.0f);
            }
        }
        if(g_espName&&e.name[0]){
            ImFont* font = espFont;
            float nameSize = g_espNameSize * s;
            ImVec2 ts=font->CalcTextSizeA(nameSize,FLT_MAX,0.f,e.name);
            float tx=cx-ts.x*0.5f,ty=bt2-ts.y-5.f*s;
            dl->AddText(font,nameSize,{tx+1.f,ty+1.f},IM_COL32(0,0,0,(int)(180*alpha)),e.name);
            dl->AddText(font,nameSize,{tx,ty},IM_COL32(240,240,245,(int)(alpha*255)),e.name);
        }
        if(e.planting||e.flashed||e.scoped||e.defusing||e.hasBomb||e.hasKits||(g_espSpotted&&e.spotted)){
            ImFont* sf = espFont;
            float tagX = br + 8.f * s;
            float tagY = bt2;
            float tagSize = 10.f * s;
            ImU32 tagCol = IM_COL32((int)(g_accentColor[0]*255),(int)(g_accentColor[1]*255),(int)(g_accentColor[2]*255),(int)(220*alpha));
            auto drawTag=[&](const char* t){
                ImVec2 tsz = sf->CalcTextSizeA(tagSize, FLT_MAX, 0.f, t);
                float padX = 5.f * s, padY = 2.f * s, chipR = 3.f * s;
                float px = tagX, py = tagY;
                float chipW = tsz.x + padX * 2.f, chipH = tsz.y + padY * 2.f;
                dl->AddRectFilled({px,py},{px+chipW,py+chipH},IM_COL32(14,16,22,(int)(210*alpha)),chipR);
                dl->AddRect({px,py},{px+chipW,py+chipH},IM_COL32((int)(g_accentColor[0]*255),(int)(g_accentColor[1]*255),(int)(g_accentColor[2]*255),(int)(72*alpha)),chipR,0,1.f);
                float ttx = px + padX, tty = py + padY;
                for(int sh=2;sh>=1;sh--) dl->AddText(sf,tagSize,{ttx+(float)sh,tty+(float)sh},IM_COL32(0,0,0,(int)(85*alpha/(float)sh)),t);
                dl->AddText(sf,tagSize,{ttx+1.f,tty+1.f},IM_COL32(0,0,0,(int)(130*alpha)),t);
                dl->AddText(sf,tagSize,{ttx,tty},tagCol,t);
                tagY += chipH + 3.f * s;
            };
            if(e.planting){ drawTag("Planting"); }
            if(e.scoped){ drawTag("Scoped"); }
            if(e.flashed){ drawTag("Flashed"); }
            if(e.defusing){ drawTag("Defusing"); }
            if(e.hasBomb){ drawTag("Bomb"); }
            if(e.hasKits){ drawTag("Kits"); }
            if(g_espSpotted && e.spotted){ drawTag("Spotted"); }
        }
        uintptr_t weapon = 0;
        WeaponInfo winfo = WeaponInfoForId(0);
        int clip = 0;
        if((g_espWeapon||g_espWeaponIcon||g_espAmmo) && entityList){
            weapon = GetActiveWeapon(e.pawn, entityList);
            if(weapon){
                int wId = GetWeaponId(weapon);
                winfo = WeaponInfoForId(wId);
                clip = GetWeaponClip(weapon);
            }
        }
        if(g_espAmmo && weapon && winfo.maxClip > 0){
            int maxClip = winfo.maxClip;
            float frac = Clampf((float)clip / (float)maxClip, 0.f, 1.f);
            float barH = 5.f * s;
            float barRound = 4.f * s;
            ImU32 ammoBg = IM_COL32((int)(g_espAmmoCol1[0]*255),(int)(g_espAmmoCol1[1]*255),(int)(g_espAmmoCol1[2]*255),(int)(230*alpha));
            ImU32 ammoBorder = IM_COL32(45,45,65,(int)(200*alpha));
            ImU32 ammoOuter = IM_COL32(0,0,0,(int)(180*alpha));
            ImU32 ammoC1=IM_COL32((int)(g_espAmmoCol1[0]*255),(int)(g_espAmmoCol1[1]*255),(int)(g_espAmmoCol1[2]*255),(int)(245*alpha));
            ImU32 ammoC2=IM_COL32((int)(g_espAmmoCol2[0]*255),(int)(g_espAmmoCol2[1]*255),(int)(g_espAmmoCol2[2]*255),(int)(245*alpha));
            for(int g=4;g>=1;g--){
                float o=(float)g; int glowA=(int)(45*alpha/(float)g);
                if(g_espAmmoStyle==0){
                    ImU32 ag1=IM_COL32((ammoC1>>IM_COL32_R_SHIFT)&0xFF,(ammoC1>>IM_COL32_G_SHIFT)&0xFF,(ammoC1>>IM_COL32_B_SHIFT)&0xFF,glowA);
                    ImU32 ag2=IM_COL32((ammoC2>>IM_COL32_R_SHIFT)&0xFF,(ammoC2>>IM_COL32_G_SHIFT)&0xFF,(ammoC2>>IM_COL32_B_SHIFT)&0xFF,glowA);
                    dl->AddRectFilledMultiColor({bl-o-1.f,belowY-o-1.f},{br+o+1.f,belowY+barH+o+1.f}, ag1,ag2, ag2,ag1);
                }else{
                    ImU32 ag=IM_COL32((ammoC2>>IM_COL32_R_SHIFT)&0xFF,(ammoC2>>IM_COL32_G_SHIFT)&0xFF,(ammoC2>>IM_COL32_B_SHIFT)&0xFF,glowA);
                    dl->AddRectFilled({bl-o-1.f,belowY-o-1.f},{br+o+1.f,belowY+barH+o+1.f},ag,barRound+o);
                }
            }
            dl->AddRectFilled({bl-2.f,belowY-2.f},{br+2.f,belowY+barH+2.f},ammoOuter,barRound+2.f);
            dl->AddRectFilled({bl-1.f,belowY-1.f},{br+1.f,belowY+barH+1.f},ammoBorder,barRound+1.f);
            dl->AddRectFilled({bl,belowY},{br,belowY+barH},ammoBg,barRound);
            if(g_espAmmoStyle==0){
                dl->AddRectFilledMultiColor({bl,belowY},{bl+bw*frac,belowY+barH}, ammoC1,ammoC2, ammoC2,ammoC1);
            }else{
                ImU32 ammoBarCol = IM_COL32((int)(g_espAmmoCol2[0]*255),(int)(g_espAmmoCol2[1]*255),(int)(g_espAmmoCol2[2]*255),(int)(245*alpha));
                dl->AddRectFilled({bl,belowY},{bl+bw*frac,belowY+barH},ammoBarCol,barRound);
            }
            if(frac > 0.01f && frac < 1.f) dl->AddRect({bl+bw*frac,belowY},{bl+bw*frac+0.5f,belowY+barH},IM_COL32(255,255,255,(int)(80*alpha)),0.f,0,1.f);
            belowY += barH + 5.f * s;
        }
        if((g_espWeapon||g_espWeaponIcon) && weapon){
            float iconW = 0.f;
            ImVec2 its = {0.f, 0.f};
            ImVec2 ts = {0.f, 0.f};
            float wepSize = 12.f * s;
            ImFont* textFont = espFont;
            ImFont* iconFont = nullptr;
            const char* iconText = nullptr;
            char iconBuf[5] = {};
            if(g_espWeaponIcon){
                ImFontBaked* gunBaked = font::gun_icons ? font::gun_icons->GetFontBaked(wepSize) : nullptr;
                if(gunBaked && winfo.iconChar && gunBaked->FindGlyphNoFallback(winfo.iconChar)){
                    WCharToUtf8(winfo.iconChar, iconBuf);
                    iconText = iconBuf;
                    iconFont = font::gun_icons;
                }else if(winfo.icon && winfo.icon[0]){
                    iconText = winfo.icon;
                    iconFont = textFont;
                }
            }
            if(iconText && iconFont){
                its = iconFont->CalcTextSizeA(wepSize, FLT_MAX, 0.f, iconText);
                iconW = its.x + (g_espWeapon ? 4.f * s : 0.f);
            }
            std::string wtext;
            if(g_espWeapon){
                wtext = winfo.name;
                if(!wtext.empty()){
                    ts = textFont->CalcTextSizeA(wepSize, FLT_MAX, 0.f, wtext.c_str());
                }
            }
            float blockLeft = cx - (iconW + ts.x)*0.5f;
            float iconYOffset = 0.f;
            if(iconText && iconFont && textFont && iconFont != textFont && (g_espWeapon && !wtext.empty())){
                ImFontBaked* iconBaked = iconFont->GetFontBaked(wepSize);
                ImFontBaked* textBaked = textFont->GetFontBaked(wepSize);
                if(iconBaked && textBaked){
                    float iconBaseline = iconBaked->Ascent;
                    float textBaseline = textBaked->Ascent;
                    iconYOffset = textBaseline - iconBaseline;
                }
            }
            if(iconText && iconFont){
                float itx = blockLeft;
                float ity = belowY + iconYOffset;
                ImU32 shadowCol = IM_COL32(0,0,0,(int)(140*alpha));
                dl->AddText(iconFont, wepSize, {itx+1.f,ity+1.f}, shadowCol, iconText);
                dl->AddText(iconFont, wepSize, {itx,ity}, dimCol, iconText);
            }
            if(g_espWeapon && !wtext.empty()){
                float tx = blockLeft + iconW;
                ImU32 shadowCol = IM_COL32(0,0,0,(int)(140*alpha));
                dl->AddText(textFont, wepSize, {tx+1.f,belowY+1.f}, shadowCol, wtext.c_str());
                dl->AddText(textFont, wepSize, {tx,belowY}, dimCol, wtext.c_str());
            }
            if(g_espWeaponIcon||g_espWeapon){
                float maxH = wepSize;
                if(iconText && iconFont) maxH = std::max(maxH, its.y);
                if(g_espWeapon && !wtext.empty()) maxH = std::max(maxH, ts.y);
                belowY += maxH + 2.f * s;
            }
        }
        if(g_espMoney){
            int money = GetPlayerMoney(e.controller);
            if(money > 0){
                char mbuf[32];
                std::snprintf(mbuf,sizeof(mbuf),"$%d", money);
                ImFont* infoFont = espFont;
                float infoSize = 12.f * s;
                ImVec2 ts=infoFont->CalcTextSizeA(infoSize, FLT_MAX, 0.f, mbuf);
                float sw=(float)g_esp_screen_w;
                float tx = (g_espMoneyPos==1) ? (sw - ts.x - 12.f * s) : (cx-ts.x*0.5f);
                float ty = (g_espMoneyPos==1) ? (bt2 + bh*0.5f - ts.y*0.5f) : belowY;
                dl->AddText(infoFont, infoSize, {tx+1.f,ty+1.f},IM_COL32(0,0,0,(int)(140*alpha)),mbuf);
                dl->AddText(infoFont, infoSize, {tx,ty},IM_COL32(120,220,120,(int)(220*alpha)),mbuf);
                if(g_espMoneyPos==0) belowY += ts.y + 2.f * s;
            }
        }
        if(g_espDist){
            char dbuf[32];snprintf(dbuf,sizeof(dbuf),"%.0fm",e.distance);
            ImFont* infoFont = espFont;
            float infoSize = 12.f * s;
            ImVec2 ts=infoFont->CalcTextSizeA(infoSize, FLT_MAX, 0.f, dbuf);float tx=cx-ts.x*0.5f;
            dl->AddText(infoFont, infoSize, {tx+1.f,belowY+1.f},IM_COL32(0,0,0,(int)(130*alpha)),dbuf);
            dl->AddText(infoFont, infoSize, {tx,belowY},dimCol,dbuf);
            belowY += ts.y + 2.f * s;
        }
    };

    for(int i=0;i<g_esp_count;i++) drawOne(g_esp_players[i], 1.f);
    bool inCur[65]={false};
    for(int i=0;i<g_esp_count;i++) if(g_esp_players[i].ent_index>=0&&g_esp_players[i].ent_index<=64) inCur[g_esp_players[i].ent_index]=true;
    UINT64 now=GetTickCount64();
    for(int j=1;j<=64;j++){
        if(inCur[j])continue;
        if(!g_esp_stale[j].valid)continue;
        if((now-g_esp_stale_tick[j])>=ESP_STALE_MS)continue;
        float t=(float)(now-g_esp_stale_tick[j])/(float)ESP_STALE_MS;
        drawOne(g_esp_stale[j], 1.f - t*0.35f);
    }
}

static void DrawOofArrows(){
    if(!g_espOof||g_esp_oof_count<=0)return;
    ImDrawList*dl=ImGui::GetForegroundDrawList();if(!dl)return;
    float sz=g_espOofSize*0.5f;
    float c=0.7f;
    for(int i=0;i<g_esp_oof_count;i++){
        float x=g_esp_oof[i].x, y=g_esp_oof[i].y, a=g_esp_oof[i].angle;
        ImU32 col=g_esp_oof[i].col;
        float co=cosf(a), si=sinf(a);
        ImVec2 tip{x + si*sz, y - co*sz};
        ImVec2 left{x - co*sz*c - si*sz*c, y - si*sz*c + co*sz*c};
        ImVec2 right{x + co*sz*c - si*sz*c, y + si*sz*c + co*sz*c};
        for(int sh=2;sh>=1;sh--){
            ImVec2 dt{(float)sh,(float)sh};
            dl->AddTriangleFilled({tip.x+dt.x,tip.y+dt.y},{left.x+dt.x,left.y+dt.y},{right.x+dt.x,right.y+dt.y},IM_COL32(0,0,0,(int)(80/(float)sh)));
        }
        dl->AddTriangleFilled(tip, left, right, col);
        dl->AddTriangle(tip, left, right, IM_COL32(0,0,0,180),1.5f);
    }
}

static void DrawRadar(){
    if(!g_radarEnabled) return;
    ImGui::SetNextWindowSize({g_radarSize,g_radarSize}, ImGuiCond_Always);
    ImGui::SetNextWindowPos({20.f,80.f}, ImGuiCond_Once);
    ImGui::SetNextWindowBgAlpha(0.6f);
    ImGuiWindowFlags wf=ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings;
    if(!g_menuOpen) wf |= ImGuiWindowFlags_NoInputs;
    ImGui::Begin("##radar", nullptr, wf);
    ImDrawList*dl=ImGui::GetWindowDrawList();if(!dl){ImGui::End();return;}
    if(ImGui::IsWindowHovered()){
        float wheel = ImGui::GetIO().MouseWheel;
        if(wheel != 0.f) g_radarRange = Clampf(g_radarRange - wheel*200.f, 300.f, 8000.f);
    }
    ImVec2 pos=ImGui::GetWindowPos();
    ImVec2 size=ImGui::GetWindowSize();
    ImVec2 center{pos.x+size.x*0.5f,pos.y+size.y*0.5f};
    dl->AddRectFilled(pos,{pos.x+size.x,pos.y+size.y},IM_COL32(10,10,14,200),6.f);
    dl->AddRect(pos,{pos.x+size.x,pos.y+size.y},IM_COL32(60,60,80,200),6.f);
    float radarRadius = size.x*0.5f-8.f;
    ImU32 ringCol = IM_COL32(80,80,110,90);
    dl->AddCircle(center, radarRadius*0.33f, ringCol, 64, 1.f);
    dl->AddCircle(center, radarRadius*0.67f, ringCol, 64, 1.f);
    dl->AddLine({center.x,pos.y+6.f},{center.x,pos.y+size.y-6.f},IM_COL32(80,80,110,120),1.f);
    dl->AddLine({pos.x+6.f,center.y},{pos.x+size.x-6.f,center.y},IM_COL32(80,80,110,120),1.f);
    uintptr_t vaAddr=ViewAnglesAddr();
    float yaw=vaAddr?Rd<float>(vaAddr+4):0.f;
    float yawRad=yaw*(3.14159265f/180.f);
    float cosY=cosf(yawRad), sinY=sinf(yawRad);
    float scale=radarRadius/Clampf(g_radarRange,300.f,8000.f);
    ImVec2 base{center.x-cosY*4.f, center.y-sinY*4.f};
    ImVec2 tip{center.x+cosY*10.f, center.y+sinY*10.f};
    ImVec2 left{base.x-sinY*5.f, base.y+cosY*5.f};
    ImVec2 right{base.x+sinY*5.f, base.y-cosY*5.f};
    dl->AddTriangleFilled(tip, left, right, IM_COL32(240,240,240,255));
    for(int i=0;i<g_esp_count;i++){
        const ESPEntry& e=g_esp_players[i];
        if(!e.valid) continue;
        Vec3 delta{e.origin_x-g_localOrigin.x,e.origin_y-g_localOrigin.y,0.f};
        float rx = delta.x*cosY + delta.y*sinY;
        float ry = -delta.x*sinY + delta.y*cosY;
        rx*=scale; ry*=scale;
        rx=Clampf(rx, -(size.x*0.5f-10.f), size.x*0.5f-10.f);
        ry=Clampf(ry, -(size.y*0.5f-10.f), size.y*0.5f-10.f);
        ImU32 col = (e.team==g_esp_local_team)?IM_COL32(80,140,255,230):IM_COL32(255,90,90,230);
        float rad = Clampf((float)e.health/100.f*5.f, 2.f, 5.f);
        ImVec2 p{center.x+rx,center.y+ry};
        dl->AddCircleFilled(p,rad,col,12);
        if(e.name[0]){
            ImFont* f = GetEspFont();
            dl->AddText(f,9.f,{p.x+4.f,p.y-4.f},IM_COL32(200,200,220,200),e.name);
        }
    }
    if(g_bombActive){
        Vec3 delta{g_bombPos.x-g_localOrigin.x,g_bombPos.y-g_localOrigin.y,0.f};
        float rx = delta.x*cosY + delta.y*sinY;
        float ry = -delta.x*sinY + delta.y*cosY;
        rx*=scale; ry*=scale;
        rx=Clampf(rx, -(size.x*0.5f-10.f), size.x*0.5f-10.f);
        ry=Clampf(ry, -(size.y*0.5f-10.f), size.y*0.5f-10.f);
        dl->AddText({center.x+rx-3.f,center.y+ry-5.f},IM_COL32(255,220,120,230),"B");
    }
    dl->AddCircleFilled(center,4.f,IM_COL32(240,240,240,255),12);
    ImGui::End();
}
