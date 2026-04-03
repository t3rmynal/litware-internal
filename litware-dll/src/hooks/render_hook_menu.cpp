// меню

static bool IsInputMessage(UINT msg){
    if(msg>=WM_MOUSEFIRST && msg<=WM_MOUSELAST) return true;
    if(msg>=WM_KEYFIRST && msg<=WM_KEYLAST) return true;
    switch(msg){
        case WM_INPUT:
        case WM_CHAR:
        case WM_SYSCHAR:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
            return true;
        default:
            return false;
    }
}

static LRESULT CALLBACK HookWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    if(msg == WM_KEYDOWN && wp == VK_F5) {
        g_showDebugConsole = !g_showDebugConsole;
        return true;
    }
    if(g_imguiInitialized&&(g_menuOpen || g_showDebugConsole)){
        if(ImGui_ImplWin32_WndProcHandler(hwnd,msg,wp,lp)) return true;
        if(IsInputMessage(msg)) return true;
    }
    if(!g_origWndProc)return DefWindowProcA(hwnd,msg,wp,lp);
    return CallWindowProcA(g_origWndProc,hwnd,msg,wp,lp);
}

static const char* KeyName(int vk){
    static char buf[32];
    if(vk==0)return "None";
    if(vk==VK_LBUTTON)return "LMB";
    if(vk==VK_RBUTTON)return "RMB";
    if(vk==VK_MBUTTON)return "MMB";
    if(vk==VK_XBUTTON1)return "X1";
    if(vk==VK_XBUTTON2)return "X2";
    UINT sc=MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_VSC);
    if(GetKeyNameTextA((LONG)(sc<<16), buf, sizeof(buf))) return buf;
    std::snprintf(buf,sizeof(buf),"VK_%d",vk);
    return buf;
}

static bool KeyBindWidget(const char* label, int* key){
    static int* capture=nullptr;
    ImGui::PushID(label);
    bool changed=false;
    const bool active = (capture==key);
    const bool hovered = ImGui::IsMouseHoveringRect(ImGui::GetCursorScreenPos(),
        {ImGui::GetCursorScreenPos().x+90.f, ImGui::GetCursorScreenPos().y+28.f});

    ImVec2 btnPos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const char* btn = active ? "..." : KeyName(*key);

    ImU32 btnBg = active
        ? IM_COL32((int)(g_accentColor[0]*255),(int)(g_accentColor[1]*255),(int)(g_accentColor[2]*255),210)
        : IM_COL32(23,23,23,220);
    if(hovered && !active)
        btnBg = IM_COL32(28,30,36,235);
    dl->AddRectFilled(btnPos, {btnPos.x+90.f, btnPos.y+26.f}, btnBg, 6.f);
    dl->AddRect(btnPos, {btnPos.x+90.f, btnPos.y+26.f}, IM_COL32(50,52,60,200), 6.f, 0, 1.f);

    ImGui::Button(btn, ImVec2(80,0));
    if(ImGui::IsItemClicked()){
        capture = key;
    }
    if(active){
        for(int vk=1; vk<256; ++vk){
            if(GetAsyncKeyState(vk)&1){
                if(vk==VK_ESCAPE)*key=0; else *key=vk;
                capture=nullptr; changed=true; break;
            }
        }
    }
    ImGui::SameLine();
    ImU32 tc = hovered ? IM_COL32(220,220,220,255) : IM_COL32(100,100,110,255);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(tc));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::PopID();
    return changed;
}

static const char* LabelTextEnd(const char* label){
    const char* end = label;
    while(*end && !(end[0]=='#' && end[1]=='#')) ++end;
    return end;
}

struct ScrollAnim {
    float target = 0.f;
    bool init = false;
};
static std::unordered_map<ImGuiID, ScrollAnim> g_scrollAnims;

static void SmoothScrollCurrentWindow(float speed = 45.f, float damping = 14.f){
    float maxY = ImGui::GetScrollMaxY();
    if(maxY <= 0.f) return;
    ImGuiID id = ImGui::GetID("##smooth_scroll");
    auto& st = g_scrollAnims[id];
    float cur = ImGui::GetScrollY();
    if(!st.init){ st.target = cur; st.init = true; }

    ImGuiIO& io = ImGui::GetIO();
    if(ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)){
        if(io.MouseWheel != 0.f){
            st.target = Clampf(st.target - io.MouseWheel * speed, 0.f, maxY);
        }else if(ImGui::IsMouseDragging(0)){
            st.target = cur;
        }
    }
    st.target = Clampf(st.target, 0.f, maxY);
    float t = 1.0f - expf(-damping * io.DeltaTime);
    float next = LerpF(cur, st.target, t);
    ImGui::SetScrollY(next);
}

static const char* SectionIconForTitle(const char* title){
    if(!title) return "";
    if(strcmp(title, "Aimbot") == 0) return "A";
    if(strcmp(title, "Triggerbot") == 0) return "T";
    if(strcmp(title, "ESP") == 0) return "E";
    if(strcmp(title, "Preview") == 0) return "P";
    if(strcmp(title, "Effects") == 0) return "F";
    if(strcmp(title, "Skins") == 0) return "S";
    if(strcmp(title, "World") == 0) return "W";
    if(strcmp(title, "Misc") == 0) return "M";
    if(strcmp(title, "Config") == 0) return "C";
    return "";
}

static void PidoSection(const char* title){
    float s = MenuScale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    if(width < 4.f) return;

    const float h    = 20.f * s;
    const float fpx  = 9.f * s;
    ImDrawList* dl   = ImGui::GetWindowDrawList();
    ImFont* reg      = font::regular ? font::regular : ImGui::GetFont();
    float midY       = pos.y + h * 0.5f;

    dl->AddLine({pos.x+2.f*s, midY}, {pos.x+width-2.f*s, midY}, IM_COL32(48,51,68,50));

    ImVec2 tsz = reg->CalcTextSizeA(fpx, FLT_MAX, 0.f, title);
    float lx   = pos.x + 10.f * s;
    dl->AddCircleFilled({lx, midY}, 1.5f*s, WithAlpha(g_pido.accent, 0.7f), 8);
    dl->AddText(reg, fpx, {lx + 4.f*s, midY - tsz.y*0.5f}, IM_COL32(95,100,125,220), title);

    ImGui::Dummy(ImVec2(width, h));
}

static bool BeginPidoChild(const char* id, const ImVec2& size){
    float scale = MenuScale();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f * scale, 8.f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 3.f * scale));
    bool open = ImGui::BeginChild(id, size, false);
    ImGui::SetWindowFontScale(scale);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p  = ImGui::GetWindowPos();
    ImVec2 sw = ImGui::GetWindowSize();
    const float rnd = g_skeetUi.childRounding * scale;
    dl->AddRectFilled(p, {p.x+sw.x, p.y+sw.y}, g_pido.elemBg, rnd);
    dl->AddRect(p, {p.x+sw.x, p.y+sw.y}, g_pido.elemStroke, rnd, 0, 1.f);
    dl->AddRect({p.x+1.f, p.y+1.f}, {p.x+sw.x-1.f, p.y+sw.y-1.f}, IM_COL32(8, 8, 12, 180), rnd, 0, 1.f);
    dl->AddLine({p.x+3.f, p.y+1.f}, {p.x+sw.x-3.f, p.y+1.f}, IM_COL32(255, 255, 255, 14));
    return open;
}

static void EndPidoChild(){
    SmoothScrollCurrentWindow(45.f * MenuScale(), 14.f);
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

static int g_grpGeneration = 0;

static bool BeginPidoGroup(const char* id, const char* title, const ImVec2& size){
    float s = MenuScale();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f*s, 2.f*s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(0.f,   2.f*s));
    bool open = ImGui::BeginChild(id, size, false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetWindowFontScale(s);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p  = ImGui::GetWindowPos();
    ImVec2 sw = ImGui::GetWindowSize();
    const float rnd = 4.f * s;

    static std::unordered_map<ImGuiID, float> s_grpAnims;
    ImGuiID gid = ImGui::GetID(id) ^ (ImGuiID)(g_grpGeneration * 0x9e3779b9u);
    float& ga = s_grpAnims[gid];
    ga = LerpF(ga, 1.f, 1.f - expf(-10.f * ImGui::GetIO().DeltaTime));

    dl->AddRectFilled(p, {p.x+sw.x, p.y+sw.y}, WithAlpha(g_pido.elemBg, ga), rnd);
    dl->AddRect(p, {p.x+sw.x, p.y+sw.y}, WithAlpha(g_pido.elemStroke, ga), rnd, 0, 1.f);

    ImFont* reg = font::regular ? font::regular : ImGui::GetFont();
    const float fpx = 9.f * s;
    const float titleH = 18.f * s;
    ImVec2 tsz = reg->CalcTextSizeA(fpx, FLT_MAX, 0.f, title);
    dl->AddText(reg, fpx, {p.x + (sw.x - tsz.x)*0.5f, p.y + (titleH - tsz.y)*0.5f},
        WithAlpha(g_pido.textDim, ga), title);
    dl->AddLine({p.x + 6.f*s, p.y + titleH}, {p.x + sw.x - 6.f*s, p.y + titleH},
        WithAlpha(g_pido.elemStroke, ga * 0.6f));

    ImGui::SetCursorPosY(titleH + 3.f * s);
    return open;
}

static void EndPidoGroup(){
    SmoothScrollCurrentWindow(45.f * MenuScale(), 14.f);
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

static bool PidoTab(const char* icon, const char* label, const char*, bool selected, float forceW = 0.f){
    ImGui::PushID(label);
    float s = MenuScale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = (forceW > 0.f) ? forceW : ImGui::GetContentRegionAvail().x;
    float height = 40.f * s;
    ImGui::InvisibleButton("##tab", ImVec2(width, height));
    bool pressed = ImGui::IsItemClicked();
    bool hovered = ImGui::IsItemHovered();
    ImVec2 nextPos = ImGui::GetCursorScreenPos();

    ImDrawList* dl = ImGui::GetWindowDrawList();

    static std::unordered_map<ImGuiID, float> s_tabDotAnims;
    ImGuiID dotId = ImGui::GetID("##dot");
    float& dotAnim = s_tabDotAnims[dotId];
    dotAnim = LerpF(dotAnim, selected ? 1.f : 0.f, 1.f - expf(-14.f * ImGui::GetIO().DeltaTime));

    ImU32 rowBg = hovered && !selected ? IM_COL32(255,255,255,6) : IM_COL32(0,0,0,0);
    dl->AddRectFilled(pos, {pos.x+width, pos.y+height}, rowBg, 0.f);
    if(selected || dotAnim > 0.01f)
        dl->AddRectFilled(pos, {pos.x+width, pos.y+height},
            IM_COL32(28, 30, 42, (int)(255.f*dotAnim)), 0.f);

    if(selected || dotAnim > 0.01f)
        dl->AddRectFilled({pos.x, pos.y+8.f*s}, {pos.x+3.5f*s*dotAnim, pos.y+height-8.f*s},
            WithAlpha(g_pido.accent, dotAnim), 2.f);

    dl->AddLine({pos.x+6.f*s, pos.y+height-0.5f}, {pos.x+width-6.f*s, pos.y+height-0.5f},
        IM_COL32(255, 255, 255, 8));

    ImU32 txtCol = selected ? g_pido.textActive : (hovered ? g_pido.text : g_pido.textDim);
    ImFont* reg = font::regular ? font::regular : ImGui::GetFont();
    const char* labelEnd = LabelTextEnd(label);
    float midY = pos.y + height * 0.5f;
    ImU32 dotCol = LerpColor(IM_COL32(50, 52, 64, 200), g_pido.accent, dotAnim);

    float tx = pos.x + 32.f*s;
    if(icon && icon[0] >= '0' && icon[0] <= '4'){
        int idx = icon[0] - '0';
        float cx = pos.x + 17.f*s;
        float r  = 7.f*s;
        ImU32 ic  = LerpColor(IM_COL32(70,73,95,220), g_pido.accent, dotAnim);
        ImU32 ic2 = LerpColor(IM_COL32(50,52,68,180), g_pido.accent, dotAnim * 0.6f);
        switch(idx){
        case 0: {
            const float tickLen = 2.8f*s, gap2 = 1.6f*s;
            dl->AddCircle({cx,midY}, r, ic, 24, 1.f*s);
            dl->AddLine({cx-r-tickLen, midY}, {cx-r-gap2, midY}, ic, 1.f*s);
            dl->AddLine({cx+r+gap2, midY}, {cx+r+tickLen, midY}, ic, 1.f*s);
            dl->AddLine({cx, midY-r-tickLen}, {cx, midY-r-gap2}, ic, 1.f*s);
            dl->AddLine({cx, midY+r+gap2}, {cx, midY+r+tickLen}, ic, 1.f*s);
            dl->AddCircleFilled({cx,midY}, 1.6f*s, ic, 8);
        }   break;
        case 1: {
            const int NS = 20;
            float eyeRx = r + 1.f*s, eyeRy = r * 0.55f;
            for(int i=0;i<NS;i++){
                float a0 = 3.14159265f + (float)i/(float)NS * 3.14159265f;
                float a1 = 3.14159265f + (float)(i+1)/(float)NS * 3.14159265f;
                dl->AddLine(
                    {cx + cosf(a0)*eyeRx, midY + sinf(a0)*eyeRy},
                    {cx + cosf(a1)*eyeRx, midY + sinf(a1)*eyeRy}, ic, 1.f*s);
            }
            for(int i=0;i<NS;i++){
                float a0 = (float)i/(float)NS * 3.14159265f;
                float a1 = (float)(i+1)/(float)NS * 3.14159265f;
                dl->AddLine(
                    {cx + cosf(a0)*eyeRx, midY + sinf(a0)*eyeRy},
                    {cx + cosf(a1)*eyeRx, midY + sinf(a1)*eyeRy}, ic, 1.f*s);
            }
            dl->AddCircleFilled({cx, midY}, r*0.32f, ic, 12);
        }   break;
        case 2: {
            dl->AddCircle({cx,midY}, r, ic, 24, 1.f*s);
            float latY1 = midY - r*0.45f, latY2 = midY + r*0.45f;
            float hw1 = sqrtf(r*r - (r*0.45f)*(r*0.45f));
            dl->AddLine({cx-hw1, latY1},{cx+hw1, latY1}, ic2, 0.8f*s);
            dl->AddLine({cx-hw1, latY2},{cx+hw1, latY2}, ic2, 0.8f*s);
            dl->AddLine({cx, midY-r},{cx, midY+r}, ic2, 0.8f*s);
        }   break;
        case 3: {
            float ri = r*0.42f, ro = r*0.72f;
            dl->AddCircle({cx,midY}, ri, ic, 16, 1.f*s);
            const int TEETH = 6;
            for(int i=0;i<TEETH;i++){
                float ang = (float)i / (float)TEETH * 3.14159265f * 2.f;
                float halfA = 3.14159265f / (float)TEETH * 0.5f;
                ImVec2 p0 = {cx + cosf(ang-halfA)*ro, midY + sinf(ang-halfA)*ro};
                ImVec2 p1 = {cx + cosf(ang+halfA)*ro, midY + sinf(ang+halfA)*ro};
                ImVec2 p2 = {cx + cosf(ang+halfA)*(ro+2.8f*s), midY + sinf(ang+halfA)*(ro+2.8f*s)};
                ImVec2 p3 = {cx + cosf(ang-halfA)*(ro+2.8f*s), midY + sinf(ang-halfA)*(ro+2.8f*s)};
                dl->AddQuad(p0, p1, p2, p3, ic, 1.f*s);
            }
        }   break;
        }
    } else {
        dl->AddCircleFilled({pos.x + 11.f*s, midY}, 2.5f*s, dotCol, 12);
        tx = pos.x + 20.f*s;
    }
    dl->AddText(reg, 12.f*s, {tx, midY - reg->LegacySize*s*0.5f}, txtCol, label, labelEnd);

    ImGui::SetCursorScreenPos(nextPos);
    ImGui::Dummy(ImVec2(0,0));
    ImGui::PopID();
    return pressed;
}

static bool PidoToggle(const char* label, const char* desc, bool* v){
    ImGui::PushID(label);
    float s = MenuScale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = g_skeetUi.rowHeight * s;
    ImGui::Dummy(ImVec2(width, height));
    ImVec2 nextPos = ImGui::GetCursorScreenPos();

    bool hovered = ImGui::IsMouseHoveringRect(pos, {pos.x+width, pos.y+height});
    if(hovered && ImGui::IsMouseClicked(0)) *v = !*v;
    bool pressed = hovered && ImGui::IsMouseClicked(0);

    static std::unordered_map<ImGuiID, float> s_toggleAnims;
    ImGuiID tid = ImGui::GetID(label);
    float& anim = s_toggleAnims[tid];
    anim = LerpF(anim, *v ? 1.f : 0.f, 1.f - expf(-18.f * ImGui::GetIO().DeltaTime));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if(hovered) dl->AddRectFilled(pos, {pos.x+width, pos.y+height}, IM_COL32(255,255,255,5), 2.f*s);

    ImFont* reg  = font::regular ? font::regular : ImGui::GetFont();
    const char* labelEnd = LabelTextEnd(label);
    float midY = pos.y + height * 0.5f;

    const float sqSz = 8.f * s;
    const float sqX  = pos.x + 8.f*s;
    const float sqY  = midY - sqSz * 0.5f;
    ImU32 sqBorder = anim > 0.01f
        ? WithAlpha(g_pido.accent, 0.7f + anim * 0.3f)
        : IM_COL32(50, 52, 72, 200);
    dl->AddRect({sqX, sqY}, {sqX+sqSz, sqY+sqSz}, sqBorder, 1.5f*s, 0, 1.f*s);
    if(anim > 0.01f)
        dl->AddRectFilled({sqX+1.f, sqY+1.f}, {sqX+sqSz-1.f, sqY+sqSz-1.f},
            WithAlpha(g_pido.accent, anim * 0.9f), 1.f*s);

    ImU32 labelCol = *v ? g_pido.textActive : g_pido.text;
    dl->AddText(reg, 11.f*s, {sqX+sqSz+6.f*s, midY-reg->LegacySize*s*0.5f}, labelCol, label, labelEnd);
    if(desc && desc[0])
        dl->AddText(reg, 9.f*s, {sqX+sqSz+6.f*s, midY+2.f*s}, g_pido.textDim, desc);

    ImGui::SetCursorScreenPos(nextPos);
    ImGui::Dummy(ImVec2(0,0));
    ImGui::PopID();
    return pressed;
}

static bool PidoSliderFloat(const char* label, const char* desc, float* v, float v_min, float v_max, const char* format = "%.1f"){
    ImGui::PushID(label);
    float s = MenuScale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = g_skeetUi.rowHeight * s;
    ImGui::Dummy(ImVec2(width, height));
    ImVec2 nextPos = ImGui::GetCursorScreenPos();

    float midY = pos.y + height * 0.5f;
    bool hovered = ImGui::IsMouseHoveringRect(pos, {pos.x+width, pos.y+height});

    const float btnSz = 12.f*s, trackW = 80.f*s, trackH = 3.f*s, gp = 4.f*s, rp = 8.f*s;
    float plusL  = pos.x + width - rp - btnSz;
    float trackR = plusL - gp;
    float trackL = trackR - trackW;
    float minusL = trackL - gp - btnSz;

    ImGui::SetCursorScreenPos({plusL, pos.y});
    ImGui::InvisibleButton("##plus", ImVec2(btnSz, height));
    bool plusHov = ImGui::IsItemHovered();
    bool plusClicked = ImGui::IsItemClicked();
    if(plusClicked) *v = Clampf(*v + (v_max-v_min)/100.f, v_min, v_max);

    ImGui::SetCursorScreenPos({minusL, pos.y});
    ImGui::InvisibleButton("##minus", ImVec2(btnSz, height));
    bool minusHov = ImGui::IsItemHovered();
    bool minusClicked = ImGui::IsItemClicked();
    if(minusClicked) *v = Clampf(*v - (v_max-v_min)/100.f, v_min, v_max);

    ImGui::SetCursorScreenPos({trackL, pos.y});
    ImGui::InvisibleButton("##track", ImVec2(trackW, height));
    bool trackActive = ImGui::IsItemActive();
    if(trackActive){
        float t = Clampf((ImGui::GetIO().MousePos.x - trackL) / trackW, 0.f, 1.f);
        *v = v_min + t * (v_max - v_min);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if(hovered) dl->AddRectFilled(pos, {pos.x+width, pos.y+height}, IM_COL32(255,255,255,4), 2.f*s);

    ImFont* reg  = font::regular ? font::regular : ImGui::GetFont();
    const char* labelEnd = LabelTextEnd(label);
    dl->AddText(reg, 11.f*s, {pos.x+8.f*s, midY-reg->LegacySize*s*0.5f}, g_pido.text, label, labelEnd);

    dl->AddRectFilled({trackL, midY-trackH*0.5f}, {trackR, midY+trackH*0.5f}, IM_COL32(28,28,42,255), trackH*0.5f);
    float t = Clampf((*v - v_min) / (v_max - v_min), 0.f, 1.f);
    float fillX = trackL + t * trackW;
    if(fillX > trackL)
        dl->AddRectFilled({trackL, midY-trackH*0.5f}, {fillX, midY+trackH*0.5f}, g_pido.accent, trackH*0.5f);

    char valBuf[32]; std::snprintf(valBuf, sizeof(valBuf), format, *v);
    ImVec2 vSz = reg->CalcTextSizeA(9.f*s, FLT_MAX, 0.f, valBuf);
    dl->AddText(reg, 9.f*s, {trackL+(trackW-vSz.x)*0.5f, midY-trackH*0.5f-vSz.y-1.f*s}, g_pido.textDim, valBuf);

    ImVec2 mSz = reg->CalcTextSizeA(11.f*s, FLT_MAX, 0.f, "-");
    ImVec2 pSz = reg->CalcTextSizeA(11.f*s, FLT_MAX, 0.f, "+");
    dl->AddText(reg, 11.f*s, {minusL+(btnSz-mSz.x)*0.5f, midY-mSz.y*0.5f},
        minusHov ? g_pido.accent : g_pido.textDim, "-");
    dl->AddText(reg, 11.f*s, {plusL+(btnSz-pSz.x)*0.5f, midY-pSz.y*0.5f},
        plusHov ? g_pido.accent : g_pido.textDim, "+");

    ImGui::SetCursorScreenPos(nextPos);
    ImGui::Dummy(ImVec2(0,0));
    ImGui::PopID();
    return trackActive || plusClicked || minusClicked;
}

static bool PidoSliderInt(const char* label, const char* desc, int* v, int v_min, int v_max, const char* format = "%d"){
    ImGui::PushID(label);
    float s = MenuScale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = g_skeetUi.rowHeight * s;
    ImGui::Dummy(ImVec2(width, height));
    ImVec2 nextPos = ImGui::GetCursorScreenPos();

    float midY = pos.y + height * 0.5f;
    bool hovered = ImGui::IsMouseHoveringRect(pos, {pos.x+width, pos.y+height});

    const float btnSz = 12.f*s, trackW = 80.f*s, trackH = 3.f*s, gp = 4.f*s, rp = 8.f*s;
    float plusL  = pos.x + width - rp - btnSz;
    float trackR = plusL - gp;
    float trackL = trackR - trackW;
    float minusL = trackL - gp - btnSz;

    ImGui::SetCursorScreenPos({plusL, pos.y});
    ImGui::InvisibleButton("##plus", ImVec2(btnSz, height));
    bool plusHov = ImGui::IsItemHovered();
    bool plusClicked = ImGui::IsItemClicked();
    if(plusClicked) *v = (std::min)(*v + 1, v_max);

    ImGui::SetCursorScreenPos({minusL, pos.y});
    ImGui::InvisibleButton("##minus", ImVec2(btnSz, height));
    bool minusHov = ImGui::IsItemHovered();
    bool minusClicked = ImGui::IsItemClicked();
    if(minusClicked) *v = (std::max)(*v - 1, v_min);

    ImGui::SetCursorScreenPos({trackL, pos.y});
    ImGui::InvisibleButton("##track", ImVec2(trackW, height));
    bool trackActive = ImGui::IsItemActive();
    if(trackActive){
        float t = Clampf((ImGui::GetIO().MousePos.x - trackL) / trackW, 0.f, 1.f);
        *v = v_min + (int)(t * (float)(v_max - v_min) + 0.5f);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if(hovered) dl->AddRectFilled(pos, {pos.x+width, pos.y+height}, IM_COL32(255,255,255,4), 2.f*s);

    ImFont* reg  = font::regular ? font::regular : ImGui::GetFont();
    const char* labelEnd = LabelTextEnd(label);
    dl->AddText(reg, 11.f*s, {pos.x+8.f*s, midY-reg->LegacySize*s*0.5f}, g_pido.text, label, labelEnd);

    dl->AddRectFilled({trackL, midY-trackH*0.5f}, {trackR, midY+trackH*0.5f}, IM_COL32(28,28,42,255), trackH*0.5f);
    float t = Clampf((float)(*v - v_min)/(float)(v_max - v_min), 0.f, 1.f);
    float fillX = trackL + t * trackW;
    if(fillX > trackL)
        dl->AddRectFilled({trackL, midY-trackH*0.5f}, {fillX, midY+trackH*0.5f}, g_pido.accent, trackH*0.5f);

    char valBuf[32]; std::snprintf(valBuf, sizeof(valBuf), format, *v);
    ImVec2 vSz = reg->CalcTextSizeA(9.f*s, FLT_MAX, 0.f, valBuf);
    dl->AddText(reg, 9.f*s, {trackL+(trackW-vSz.x)*0.5f, midY-trackH*0.5f-vSz.y-1.f*s}, g_pido.textDim, valBuf);

    ImVec2 mSz = reg->CalcTextSizeA(11.f*s, FLT_MAX, 0.f, "-");
    ImVec2 pSz = reg->CalcTextSizeA(11.f*s, FLT_MAX, 0.f, "+");
    dl->AddText(reg, 11.f*s, {minusL+(btnSz-mSz.x)*0.5f, midY-mSz.y*0.5f},
        minusHov ? g_pido.accent : g_pido.textDim, "-");
    dl->AddText(reg, 11.f*s, {plusL+(btnSz-pSz.x)*0.5f, midY-pSz.y*0.5f},
        plusHov ? g_pido.accent : g_pido.textDim, "+");

    ImGui::SetCursorScreenPos(nextPos);
    ImGui::Dummy(ImVec2(0,0));
    ImGui::PopID();
    return trackActive || plusClicked || minusClicked;
}

static bool PidoCombo(const char* label, const char* desc, int* current_item, const char* const items[], int items_count){
    ImGui::PushID(label);
    float s = MenuScale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = g_skeetUi.rowHeight * s;

    ImGui::InvisibleButton("##row", ImVec2(width, height));
    bool rowHovered  = ImGui::IsItemHovered();
    bool rowClicked  = ImGui::IsItemClicked();
    ImVec2 nextPos   = ImGui::GetCursorScreenPos();

    ImVec2 bbMin = pos, bbMax{pos.x+width, pos.y+height};
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImU32 rowBg = rowHovered ? IM_COL32(255,255,255,6) : IM_COL32(0,0,0,0);
    dl->AddRectFilled(bbMin, bbMax, rowBg, 2.f*s);
    dl->AddLine({pos.x+6.f*s, pos.y+height-0.5f}, {pos.x+width-6.f*s, pos.y+height-0.5f}, IM_COL32(255,255,255,12));

    ImFont* bold = font::bold ? font::bold : ImGui::GetFont();
    ImFont* reg  = font::regular ? font::regular : ImGui::GetFont();
    const char* labelEnd = LabelTextEnd(label);
    float midY = pos.y + height * 0.5f;
    dl->AddText(bold, 12.f*s, {pos.x+8.f*s, midY - 6.f*s}, g_pido.textActive, label, labelEnd);

    const char* curItem = (*current_item >= 0 && *current_item < items_count) ? items[*current_item] : "";
    const float chipPadX = 7.f*s, chipPadY = 2.f*s, chipRnd = 4.f*s, arrowW = 10.f*s;
    ImVec2 chipSz = reg->CalcTextSizeA(11.f*s, FLT_MAX, 0.f, curItem);
    float chipW = chipSz.x + chipPadX*2.f + arrowW + 4.f*s;
    float chipH = chipSz.y + chipPadY*2.f;
    float chipX = bbMax.x - chipW - 10.f*s;
    float chipY = midY - chipH*0.5f;
    dl->AddRectFilled({chipX, chipY}, {chipX+chipW, chipY+chipH}, IM_COL32(22,24,32,255), chipRnd);
    dl->AddRect({chipX, chipY}, {chipX+chipW, chipY+chipH}, WithAlpha(g_pido.accent, 0.35f), chipRnd, 0, 1.f);
    dl->AddText(reg, 11.f*s, {chipX+chipPadX, chipY+chipPadY}, g_pido.text, curItem);

    const char* popupId = "##pido_combo_popup";
    if(rowClicked) ImGui::OpenPopup(popupId);
    bool popup_open = ImGui::IsPopupOpen(popupId);

    float ax = chipX + chipW - arrowW*0.5f - 2.f*s;
    if(popup_open)
        dl->AddTriangleFilled({ax-3.f*s,midY+2.f*s},{ax+3.f*s,midY+2.f*s},{ax,midY-2.f*s}, g_pido.textDim);
    else
        dl->AddTriangleFilled({ax-3.f*s,midY-2.f*s},{ax+3.f*s,midY-2.f*s},{ax,midY+2.f*s}, g_pido.textDim);

    ImGui::SetNextWindowPos({pos.x, bbMax.y + 2.f*s});
    ImGui::SetNextWindowSize({width, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.f*s, 5.f*s));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 6.f*s);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(17, 18, 24, 252));
    ImGui::PushStyleColor(ImGuiCol_Border,  WithAlpha(g_pido.accent, 0.3f));

    bool changed = false;
    if(ImGui::BeginPopup(popupId)){
        const float lineH = reg->LegacySize + 8.f*s;
        for(int i = 0; i < items_count; i++){
            ImGui::PushID(i);
            bool selected = (*current_item == i);
            ImVec2 ipos = ImGui::GetCursorScreenPos();
            float iw = ImGui::GetContentRegionAvail().x;
            ImGui::InvisibleButton("##item", ImVec2(iw, lineH));
            bool iHov = ImGui::IsItemHovered();
            if(ImGui::IsItemClicked()){
                *current_item = i;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImDrawList* pdl = ImGui::GetWindowDrawList();
            if(selected){
                pdl->AddRectFilled(ipos, {ipos.x+iw, ipos.y+lineH}, WithAlpha(g_pido.accent, 0.22f), 3.f*s);
            } else if(iHov){
                pdl->AddRectFilled(ipos, {ipos.x+iw, ipos.y+lineH}, IM_COL32(255,255,255,8), 3.f*s);
            }
            ImU32 itemCol = selected ? g_pido.accent : (iHov ? g_pido.textActive : g_pido.text);
            pdl->AddText(reg, reg->LegacySize, {ipos.x + 5.f*s, ipos.y + (lineH - reg->LegacySize)*0.5f}, itemCol, items[i]);
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    ImGui::SetCursorScreenPos(nextPos);
    ImGui::Dummy(ImVec2(0,0));
    ImGui::PopID();
    return changed;
}

static bool PidoColorEdit4(const char* label, const char* desc, float col[4], ImGuiColorEditFlags flags = 0){
    ImGui::PushID(label);
    float s = MenuScale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = 44.f * s;
    ImVec2 bbMin = pos, bbMax{pos.x+width, pos.y+height};
    bool hovered = ImGui::IsMouseHoveringRect(bbMin, bbMax);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 rowBg = hovered ? IM_COL32(255,255,255,6) : IM_COL32(0,0,0,0);
    dl->AddRectFilled(bbMin, bbMax, rowBg, 2.f*s);
    dl->AddLine({pos.x+6.f*s, pos.y+height-0.5f}, {pos.x+width-6.f*s, pos.y+height-0.5f}, IM_COL32(255,255,255,12));

    ImFont* bold = font::bold ? font::bold : ImGui::GetFont();
    const char* labelEnd = LabelTextEnd(label);
    float midY = pos.y + height * 0.5f;
    dl->AddText(bold, 13.f*s, {pos.x+12.f*s, midY - 6.5f*s}, g_pido.textActive, label, labelEnd);

    const float swW = 22.f*s, swH = 22.f*s, swRnd = 4.f*s;
    float swX = bbMax.x - swW - 10.f*s;
    float swY = midY - swH*0.5f;
    ImU32 chk1 = IM_COL32(60,60,60,255), chk2 = IM_COL32(40,40,40,255);
    float cw = swW / 4.f, ch = swH / 2.f;
    for(int ci=0; ci<4; ci++){
        float cx2 = swX + ci*cw;
        dl->AddRectFilled({cx2, swY},      {cx2+cw, swY+ch}, (ci%2==0)?chk1:chk2, 0);
        dl->AddRectFilled({cx2, swY+ch},   {cx2+cw, swY+swH}, (ci%2==0)?chk2:chk1, 0);
    }
    dl->AddRectFilled({swX,swY},{swX+swW,swY+swH}, IM_COL32((int)(col[0]*255),(int)(col[1]*255),(int)(col[2]*255),(int)(col[3]*255)), swRnd);
    dl->AddRect({swX,swY},{swX+swW,swY+swH}, g_pido.elemStroke, swRnd, 0, 1.f);

    ImGui::SetCursorScreenPos({swX, swY});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, swRnd);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0,0));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0,0,0,0));
    bool changed = ImGui::ColorEdit4("##col", col, flags | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    ImGui::SetCursorScreenPos({pos.x, bbMax.y});
    ImGui::Dummy(ImVec2(0,0));
    ImGui::PopID();
    return changed;
}

static bool PidoInputText(const char* label, const char* desc, char* buf, size_t bufSize){
    ImGui::PushID(label);
    float s = MenuScale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = 34.f * s;
    ImGui::Dummy(ImVec2(width, height));
    ImVec2 nextPos = ImGui::GetCursorScreenPos();
    ImVec2 bbMin = pos;
    ImVec2 bbMax{pos.x+width, pos.y+height};
    bool hovered = ImGui::IsMouseHoveringRect(bbMin, bbMax);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg = hovered ? IM_COL32(255,255,255,6) : IM_COL32(0,0,0,0);
    dl->AddRectFilled(bbMin, bbMax, bg, 2.f * s);
    dl->AddLine({pos.x+6.f*s, pos.y+height-0.5f}, {pos.x+width-6.f*s, pos.y+height-0.5f}, IM_COL32(255,255,255,12));

    ImFont* bold = font::bold ? font::bold : ImGui::GetFont();
    ImFont* reg = font::regular ? font::regular : ImGui::GetFont();
    const char* labelEnd = LabelTextEnd(label);
    float labelY = (desc && desc[0]) ? pos.y+2.f*s : pos.y + (height - bold->LegacySize*s)*0.5f;
    dl->AddText(bold, 12.f * s, {pos.x+8.f*s, labelY}, g_pido.textActive, label, labelEnd);
    if(desc && desc[0]) dl->AddText(reg, 10.f * s, {pos.x+8.f*s, pos.y+14.f*s}, g_pido.textDim, desc);

    float inputW = (std::min)(200.f * s, width * 0.55f);
    ImGui::SetCursorScreenPos({bbMax.x - inputW - 8.f*s, bbMin.y + (height - 22.f*s)*0.5f});
    ImGui::PushItemWidth(inputW);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f * s);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(g_pido.elemBg));
    bool changed = ImGui::InputText("##input", buf, bufSize, ImGuiInputTextFlags_AutoSelectAll);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::PopItemWidth();

    ImGui::SetCursorScreenPos(nextPos);
    ImGui::Dummy(ImVec2(0,0));
    ImGui::PopID();
    return changed;
}

static bool PidoInputInt(const char* label, const char* desc, int* v){
    ImGui::PushID(label);
    float s = MenuScale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = 34.f * s;
    ImGui::Dummy(ImVec2(width, height));
    ImVec2 nextPos = ImGui::GetCursorScreenPos();
    ImVec2 bbMin = pos;
    ImVec2 bbMax{pos.x+width, pos.y+height};
    bool hovered = ImGui::IsMouseHoveringRect(bbMin, bbMax);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg = hovered ? IM_COL32(255,255,255,6) : IM_COL32(0,0,0,0);
    dl->AddRectFilled(bbMin, bbMax, bg, 2.f * s);
    dl->AddLine({pos.x+6.f*s, pos.y+height-0.5f}, {pos.x+width-6.f*s, pos.y+height-0.5f}, IM_COL32(255,255,255,12));

    ImFont* bold = font::bold ? font::bold : ImGui::GetFont();
    ImFont* reg = font::regular ? font::regular : ImGui::GetFont();
    const char* labelEnd = LabelTextEnd(label);
    float labelY = (desc && desc[0]) ? pos.y+2.f*s : pos.y + (height - bold->LegacySize*s)*0.5f;
    dl->AddText(bold, 12.f * s, {pos.x+8.f*s, labelY}, g_pido.textActive, label, labelEnd);
    if(desc && desc[0]) dl->AddText(reg, 10.f * s, {pos.x+8.f*s, pos.y+14.f*s}, g_pido.textDim, desc);

    float inputW = (std::min)(200.f * s, width * 0.55f);
    ImGui::SetCursorScreenPos({bbMax.x - inputW - 8.f*s, bbMin.y + (height - 22.f*s)*0.5f});
    ImGui::PushItemWidth(inputW);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f * s);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(g_pido.elemBg));
    bool changed = ImGui::InputInt("##input", v, 1, 10);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::PopItemWidth();

    ImGui::SetCursorScreenPos(nextPos);
    ImGui::Dummy(ImVec2(0,0));
    ImGui::PopID();
    return changed;
}

static bool PidoKeybind(const char* label, const char* desc, int* key){
    static int* capture = nullptr;
    ImGui::PushID(label);
    float s = MenuScale();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = 34.f * s;
    ImGui::Dummy(ImVec2(width, height));
    ImVec2 nextPos = ImGui::GetCursorScreenPos();
    ImVec2 bbMin = pos;
    ImVec2 bbMax{pos.x+width, pos.y+height};
    bool hovered = ImGui::IsMouseHoveringRect(bbMin, bbMax);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg = hovered ? IM_COL32(255,255,255,6) : IM_COL32(0,0,0,0);
    dl->AddRectFilled(bbMin, bbMax, bg, 2.f * s);
    dl->AddLine({pos.x+6.f*s, pos.y+height-0.5f}, {pos.x+width-6.f*s, pos.y+height-0.5f}, IM_COL32(255,255,255,12));

    ImFont* bold = font::bold ? font::bold : ImGui::GetFont();
    ImFont* reg = font::regular ? font::regular : ImGui::GetFont();
    const char* labelEnd = LabelTextEnd(label);
    float labelY = (desc && desc[0]) ? pos.y+2.f*s : pos.y + (height - bold->LegacySize*s)*0.5f;
    dl->AddText(bold, 12.f * s, {pos.x+8.f*s, labelY}, g_pido.textActive, label, labelEnd);
    if(desc && desc[0]) dl->AddText(reg, 10.f * s, {pos.x+8.f*s, pos.y+14.f*s}, g_pido.textDim, desc);

    float btnW = 76.f * s, btnH = 22.f * s;
    ImGui::SetCursorScreenPos({bbMax.x - btnW - 8.f*s, bbMin.y + (height - btnH)*0.5f});
    const char* btn = (capture == key) ? "..." : KeyName(*key);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f * s);
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(g_pido.elemBg));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(35.f/255.f,35.f/255.f,50.f/255.f,1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(g_pido.accent));
    if(ImGui::Button(btn, ImVec2(btnW, btnH))){
        capture = key;
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();

    if(capture == key){
        for(int vk=1; vk<256; ++vk){
            if(GetAsyncKeyState(vk)&1){
                if(vk==VK_ESCAPE) *key = 0; else *key = vk;
                capture = nullptr;
                break;
            }
        }
    }

    ImGui::SetCursorScreenPos(nextPos);
    ImGui::Dummy(ImVec2(0,0));
    ImGui::PopID();
    return false;
}

static bool PidoButton(const char* label, const ImVec2& size){
    float s = MenuScale();
    ImVec2 sz = size;
    if(sz.x > 0.f) sz.x *= s;
    if(sz.y > 0.f) sz.y *= s;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f * s);
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(g_pido.elemBg));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(g_pido.tabActive));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(g_pido.accent));
    bool pressed = ImGui::Button(label, sz);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    return pressed;
}

static void DrawFakeESPPreview(const ImVec2& size, int pos){
    (void)pos;
    ImGui::BeginChild("##esp_preview", size, true);
    ImDrawList*dl=ImGui::GetWindowDrawList();
    ImVec2 p=ImGui::GetWindowPos();
    ImVec2 s=ImGui::GetWindowSize();
    ImVec2 c{p.x+s.x*0.5f,p.y+s.y*0.55f};
    float boxH=s.y*0.6f,boxW=boxH*0.42f;
    ImU32 eCol=IM_COL32((int)(g_espEnemyCol[0]*255),(int)(g_espEnemyCol[1]*255),(int)(g_espEnemyCol[2]*255),255);
    ImU32 tCol=IM_COL32((int)(g_espTeamCol[0]*255),(int)(g_espTeamCol[1]*255),(int)(g_espTeamCol[2]*255),255);
    const float prvRnd = 3.5f;
    auto drawBox=[&](ImVec2 center,ImU32 col){
        float l=center.x-boxW*0.5f,r=center.x+boxW*0.5f,t=center.y-boxH*0.5f,b=center.y+boxH*0.5f;
        if(g_espBoxShadow){
            dl->AddRectFilled({l+4.f,t+4.f},{r+4.f,b+4.f},IM_COL32(0,0,0,34),prvRnd+1.5f);
            dl->AddRectFilled({l+2.f,t+2.f},{r+2.f,b+2.f},IM_COL32(0,0,0,48),prvRnd+0.5f);
        }
        if(g_espBoxStyle==2){
            dl->AddRectFilled({l,t},{r,b},IM_COL32(20,20,28,140),prvRnd);
            DrawCornerBox(dl,l,t,r,b,IM_COL32(0,0,0,200),g_espBoxThick+1.f);
            DrawCornerBox(dl,l,t,r,b,col,g_espBoxThick);
        }else if(g_espBoxStyle==1){
            dl->AddRect({l,t},{r,b},IM_COL32(0,0,0,200),prvRnd,0,g_espBoxThick+1.f);
            dl->AddRect({l,t},{r,b},col,prvRnd,0,g_espBoxThick);
        }else if(g_espBoxStyle==3){
            DrawOutlineBox(dl,l,t,r,b,col,g_espBoxThick);
        }else if(g_espBoxStyle==4){
            DrawCoalBox(dl,l,t,r,b,IM_COL32(0,0,0,200),g_espBoxThick+1.f);
            DrawCoalBox(dl,l,t,r,b,col,g_espBoxThick);
        }else if(g_espBoxStyle==5){
            DrawOutlineCoalBox(dl,l,t,r,b,col,g_espBoxThick);
        }else{
            DrawCornerBox(dl,l,t,r,b,IM_COL32(0,0,0,200),g_espBoxThick+1.f);
            DrawCornerBox(dl,l,t,r,b,col,g_espBoxThick);
        }
        if(g_espHeadDot){
            float dotR = 7.f;
            dl->AddCircle({center.x, t+6.f},dotR,IM_COL32(0,0,0,180),16,1.5f);
            dl->AddCircleFilled({center.x, t+6.f},dotR*0.55f,col,12);
            dl->AddCircle({center.x, t+6.f},dotR,col,16,1.0f);
        }
        if(g_espHealth){
            float barW=5.f;float bx=l-8.f-barW;
            dl->AddRectFilled({bx-1.f,t-1.f},{bx+barW+1.f,b+1.f},IM_COL32(40,42,52,200),3.f);
            dl->AddRectFilled({bx,t},{bx+barW,b},IM_COL32(10,10,14,220),2.f);
            dl->AddRectFilled({bx,t+boxH*0.3f},{bx+barW,b},HealthCol(70),2.f);
        }
    };
    drawBox({c.x-60.f,c.y}, eCol);
    drawBox({c.x+60.f,c.y}, eCol);
    drawBox({c.x,c.y+10.f}, tCol);
    ImGui::EndChild();
}

static void DrawMenu(){
    ImGuiIO&io=ImGui::GetIO();
    float target = g_menuOpen ? 1.f : 0.f;
    float speed = Clampf(g_menuAnimSpeed, 2.f, 30.f);
    float t = 1.0f - expf(-speed * io.DeltaTime);
    g_menuAnim = LerpF(g_menuAnim, target, t);
    g_menuAnim = Clampf(g_menuAnim, 0.f, 1.f);
    if(!g_menuOpen && g_menuAnim < 0.01f) return;

    UpdatePidoraisePalette(g_menuAnim);

    static int page = g_activeTab;

    float menuScale = MenuScale();
    float animEased = sqrtf(1.f - powf(g_menuAnim - 1.f, 2.f));
    float animScale = LerpF(0.92f, 1.f, animEased);
    float menuW = 820.f * menuScale * animScale;
    float menuH = 540.f * menuScale * animScale;
    float sw=(float)g_esp_screen_w, sh=(float)g_esp_screen_h;
    if(sw < 100.f || sh < 100.f){
        if(g_bbWidth >= 100){ sw = (float)g_bbWidth; } else if(sw < 100.f) sw = 1920.f;
        if(g_bbHeight >= 100){ sh = (float)g_bbHeight; } else if(sh < 100.f) sh = 1080.f;
    }

    ImGui::SetNextWindowSize(ImVec2(menuW,menuH), ImGuiCond_Always);
    float slideY = (1.f - animEased) * 10.f * menuScale;
    float menuX = (sw - menuW) * 0.5f;
    float menuY = (sh - menuH) * 0.5f + slideY;
    menuX = Clampf(menuX, 0.f, sw - menuW);
    menuY = Clampf(menuY, 0.f, sh - menuH);
    ImGui::SetNextWindowPos(ImVec2(menuX, menuY), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, animEased);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0,0,0,0));

    if(!ImGui::Begin("##menu",nullptr,flags)){
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(4);
        return;
    }

    if(font::bold) ImGui::PushFont(font::bold);

    ImDrawList*dl=ImGui::GetWindowDrawList();
    ImVec2 pos=ImGui::GetWindowPos();
    ImVec2 size=ImGui::GetWindowSize();

    const float s = menuScale;
    ImGui::SetWindowFontScale(s);
    const float menuRounding  = 0.f;
    const float pad           = 10.f * s;
    const float headerH       = 32.f * s;
    const float tabBarH       = 26.f * s;

    dl->AddRectFilled(pos, {pos.x+size.x, pos.y+size.y}, g_pido.bgFill, menuRounding);
    dl->AddRect(pos, {pos.x+size.x, pos.y+size.y},
        WithAlpha(g_pido.accent, 0.55f * animEased), menuRounding, 0, 1.f);
    dl->AddRect({pos.x+1.f, pos.y+1.f}, {pos.x+size.x-1.f, pos.y+size.y-1.f},
        IM_COL32(4, 5, 8, (int)(200.f*animEased)), menuRounding);

    {
        const float gy0 = pos.y + 1.f, gy1 = pos.y + 3.f;
        float gx0 = pos.x + 2.f, gx1 = pos.x + size.x - 2.f;
        float w = gx1 - gx0;
        float gq1 = gx0 + w * 0.33f, gq2 = gx0 + w * 0.66f;
        auto fa = [&](ImU32 c) -> ImU32 { return WithAlpha(c, animEased); };
        dl->AddRectFilledMultiColor({gx0,gy0},{gq1,gy1},
            fa(IM_COL32(108,132,188,220)), fa(IM_COL32(174,122,190,220)),
            fa(IM_COL32(174,122,190,220)), fa(IM_COL32(108,132,188,220)));
        dl->AddRectFilledMultiColor({gq1,gy0},{gq2,gy1},
            fa(IM_COL32(174,122,190,220)), fa(IM_COL32(194,166,118,220)),
            fa(IM_COL32(194,166,118,220)), fa(IM_COL32(174,122,190,220)));
        dl->AddRectFilledMultiColor({gq2,gy0},{gx1,gy1},
            fa(IM_COL32(194,166,118,220)), fa(IM_COL32(116,168,148,220)),
            fa(IM_COL32(116,168,148,220)), fa(IM_COL32(194,166,118,220)));
    }

    dl->AddLine({pos.x, pos.y+headerH}, {pos.x+size.x, pos.y+headerH},
        WithAlpha(g_pido.elemStroke, animEased), 1.f);
    float tabBarTop2 = pos.y + size.y - tabBarH;
    dl->AddRectFilled({pos.x, tabBarTop2}, {pos.x+size.x, pos.y+size.y}, g_pido.tabBg, 0.f);
    dl->AddLine({pos.x, tabBarTop2}, {pos.x+size.x, tabBarTop2},
        WithAlpha(g_pido.elemStroke, animEased), 1.f);

    {
        ImFont* fBold = font::bold ? font::bold : ImGui::GetFont();
        ImFont* fReg  = font::regular ? font::regular : fBold;
        float midH = pos.y + headerH * 0.5f;
        ImU32 dimFaded = WithAlpha(g_pido.textDim, animEased);

        const char* title = "litware";
        const float tSize = 15.f * s;
        ImVec2 szT = fBold->CalcTextSizeA(tSize, FLT_MAX, 0.f, title);
        dl->AddText(fBold, tSize, {pos.x + (size.x - szT.x)*0.5f, midH - szT.y*0.5f},
            WithAlpha(g_pido.textActive, animEased), title);

        const char* verBuf  = "v0.1.5";
        const char* dateBuf = "release: 03.04.2026";
        ImVec2 szVer  = fReg->CalcTextSizeA(9.f*s, FLT_MAX, 0.f, verBuf);
        ImVec2 szDate = fReg->CalcTextSizeA(9.f*s, FLT_MAX, 0.f, dateBuf);
        float lx = pos.x + 12.f*s;
        dl->AddText(fReg, 9.f*s, {lx, midH - szVer.y*0.5f},
            WithAlpha(g_pido.textActive, animEased), verBuf);
        lx += szVer.x + 6.f*s;
        dl->AddCircleFilled({lx, midH}, 1.3f*s, dimFaded, 8);
        lx += 6.f*s;
        dl->AddText(fReg, 9.f*s, {lx, midH - szDate.y*0.5f}, dimFaded, dateBuf);

        SYSTEMTIME st{}; GetLocalTime(&st);
        char timeBuf[12], fpsBuf2[16];
        std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", st.wHour, st.wMinute);
        std::snprintf(fpsBuf2, sizeof(fpsBuf2), "%.0f fps", io.Framerate);
        ImVec2 szTime2 = fReg->CalcTextSizeA(9.f*s, FLT_MAX, 0.f, timeBuf);
        ImVec2 szFps2  = fReg->CalcTextSizeA(9.f*s, FLT_MAX, 0.f, fpsBuf2);
        float rx = pos.x + size.x - 12.f*s;
        rx -= szFps2.x;
        dl->AddText(fReg, 9.f*s, {rx, midH - szFps2.y*0.5f}, dimFaded, fpsBuf2);
        rx -= 8.f*s;
        dl->AddCircleFilled({rx, midH}, 1.3f*s, dimFaded, 8);
        rx -= szTime2.x + 8.f*s;
        dl->AddText(fReg, 9.f*s, {rx, midH - szTime2.y*0.5f}, dimFaded, timeBuf);
    }
    {
        const char* tabLabels2[] = {"Aimbot","Visuals","World","Misc"};
        ImFont* fTabReg = font::regular ? font::regular : ImGui::GetFont();
        float tabBarTop = pos.y + size.y - tabBarH;
        float eachW = size.x / 4.f;
        for(int i=0;i<4;i++){
            float tx = pos.x + i*eachW;
            ImGui::SetCursorScreenPos({tx, tabBarTop});
            ImGui::InvisibleButton(tabLabels2[i], ImVec2(eachW, tabBarH));
            if(ImGui::IsItemClicked()) page=i;
            bool tabSel=(page==i);
            float fpx=tabSel?11.f*s:10.f*s;
            ImU32 tCol=tabSel?WithAlpha(g_pido.accent,animEased):WithAlpha(g_pido.textDim,animEased);
            ImVec2 tSz=fTabReg->CalcTextSizeA(fpx,FLT_MAX,0.f,tabLabels2[i]);
            float tlx=tx+(eachW-tSz.x)*0.5f;
            float tly=tabBarTop+(tabBarH-tSz.y)*0.5f;
            dl->AddText(fTabReg,fpx,{tlx,tly},tCol,tabLabels2[i]);
            if(tabSel)
                dl->AddLine({tlx, tabBarTop+tabBarH-2.f*s},
                            {tlx+tSz.x, tabBarTop+tabBarH-2.f*s},
                            WithAlpha(g_pido.accent,animEased), 1.5f*s);
            if(i>0)
                dl->AddLine({tx, tabBarTop+5.f*s}, {tx, tabBarTop+tabBarH-5.f*s},
                    WithAlpha(g_pido.elemStroke, animEased * 0.6f));
        }
    }

    if(page != g_activeTab){
        g_grpGeneration++;
        g_activeTab = page;
    }

    static bool s_configTabWasVisible = false;
    const bool configTabVisible = g_activeTab == 3;
    if(configTabVisible && !s_configTabWasVisible) RefreshConfigList();
    s_configTabWasVisible = configTabVisible;

    const float tabAlphaEased = 1.f;

    float contentX = pad;
    float contentY = headerH + pad;
    float contentW = size.x - pad*2.f;
    float contentH = (std::max)(80.f*s, size.y - headerH - tabBarH - pad*2.f);
    const float gap  = 6.f * s;
    float childW = (contentW - gap) * 0.5f;
    float rightX = contentX + childW + gap;
    auto grpH = [&](int rows) -> float {
        return rows * g_skeetUi.rowHeight * s + 30.f * s;
    };

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, animEased * tabAlphaEased);

    if(g_activeTab==0){
        ImGui::SetCursorPos({contentX, contentY});
        BeginPidoGroup("##g_aim", "Aimbot", {childW, contentH});
        PidoToggle("Enable","", &g_aimbotEnabled);
        if(g_aimbotEnabled){
            const char* aimBoneItems[] = {"Head","Neck","Chest","Stomach","Pelvis"};
            PidoSliderFloat("FOV","", &g_aimbotFov, 1.f, 90.f, "%.1f");
            PidoSliderFloat("Smooth","", &g_aimbotSmooth, 1.f, 30.f, "%.1f");
            PidoCombo("Bone","", &g_aimbotBone, aimBoneItems, IM_ARRAYSIZE(aimBoneItems));
            PidoKeybind("Aimbot key","", &g_aimbotKey);
            PidoToggle("FOV circle","", &g_fovCircleEnabled);
            if(g_fovCircleEnabled) PidoColorEdit4("FOV color","", g_fovCircleCol);
            PidoToggle("Autostop","", &g_autostopEnabled);
            PidoToggle("Wait aim then fire","", &g_waitAimThenFire);
            if(g_waitAimThenFire) PidoSliderFloat("Aim lock (deg)","", &g_waitAimFovDeg, 0.5f, 8.f, "%.2f");
            PidoToggle("Visible only","", &g_aimbotVisCheck);
        }
        EndPidoGroup();

        float tbH  = grpH(3);
        float rcsH = contentH - tbH - gap;
        ImGui::SetCursorPos({rightX, contentY});
        BeginPidoGroup("##g_tb", "Triggerbot", {childW, tbH});
        PidoToggle("Enable##tb","", &g_tbEnabled);
        PidoSliderInt("Delay (ms)","", &g_tbDelay, 0, 300);
        PidoKeybind("Trigger key","", &g_tbKey);
        EndPidoGroup();

        ImGui::SetCursorPos({rightX, contentY + tbH + gap});
        BeginPidoGroup("##g_rcs", "Recoil Control", {childW, rcsH});
        PidoToggle("Enable##rcs","", &g_rcsEnabled);
        PidoSliderFloat("X axis","", &g_rcsX, 0.f, 2.f, "%.2f");
        PidoSliderFloat("Y axis","", &g_rcsY, 0.f, 2.f, "%.2f");
        PidoSliderFloat("Smooth","", &g_rcsSmooth, 1.f, 20.f, "%.1f");
        EndPidoGroup();
    }else if(g_activeTab==1){
        ImGui::SetCursorPos({contentX, contentY});
        BeginPidoGroup("##g_esp", "ESP", {childW, contentH});
        PidoToggle("Enable##esp","", &g_espEnabled);
        if(g_safeMode){ ImGui::SameLine(); if(ImGui::Button("Retry##esp2")) g_safeMode=false; }
        PidoSection("Box");
        {
            PidoToggle("Draw box","", &g_espDrawBox);
            const char* boxItems[]={"Corner","Full","Corner Fill","Outline","Coal","Outline Coal"};
            PidoCombo("Style","", &g_espBoxStyle, boxItems, IM_ARRAYSIZE(boxItems));
            PidoSliderFloat("Thickness","", &g_espBoxThick, 0.5f, 4.f, "%.1f");
            PidoToggle("Shadow","", &g_espBoxShadow);
            PidoColorEdit4("Enemy color","", g_espEnemyCol);
            PidoColorEdit4("Team color","", g_espTeamCol);
        }
        PidoSection("Labels");
        {
            PidoToggle("Name","", &g_espName);
            if(g_espName) PidoSliderFloat("Name size","", &g_espNameSize, 10.f, 18.f, "%.1f");
            PidoToggle("Health","", &g_espHealth);
            if(g_espHealth){
                const char* hStyles[]={"Gradient","Solid"};
                PidoCombo("Health style","", &g_espHealthStyle, hStyles, 2);
                if(g_espHealthStyle==0){
                    PidoColorEdit4("HP full","", g_espHealthGradientCol1);
                    PidoColorEdit4("HP empty","", g_espHealthGradientCol2);
                }
            }
            PidoToggle("Distance","", &g_espDist);
            PidoToggle("Money","", &g_espMoney);
            PidoToggle("Weapon","", &g_espWeapon);
            PidoToggle("Weapon icon","", &g_espWeaponIcon);
        }
        PidoSection("Extra");
        {
            PidoToggle("Only visible","", &g_espOnlyVis);
            PidoToggle("Teammates","", &g_espShowTeam);
            PidoToggle("Head dot","", &g_espHeadDot);
            PidoToggle("Spotted tag","", &g_espSpotted);
            PidoToggle("OOF arrows","", &g_espOof);
            if(g_espOof) PidoSliderFloat("OOF size","", &g_espOofSize, 12.f, 48.f, "%.0f");
            PidoToggle("Lines","", &g_espLines);
            if(g_espLines){ const char* lineAnchors[]={"Top","Middle","Bottom"}; PidoCombo("Line anchor","", &g_espLineAnchor, lineAnchors, 3); }
            PidoToggle("Ammo bar","", &g_espAmmo);
            if(g_espAmmo){
                const char* aStyles[]={"Gradient","Solid"};
                PidoCombo("Ammo style","", &g_espAmmoStyle, aStyles, 2);
                if(g_espAmmoStyle==0){
                    PidoColorEdit4("Ammo empty","", g_espAmmoCol1);
                    PidoColorEdit4("Ammo full","", g_espAmmoCol2);
                }
            }
            PidoToggle("Damage numbers","", &g_damageFloatersEnabled);
            if(g_damageFloatersEnabled){
                PidoSliderFloat("Dmg duration","", &g_damageFloaterDuration, 0.35f, 2.f, "%.2f");
                PidoSliderFloat("Dmg scale","", &g_damageFloaterScale, 0.5f, 2.f, "%.2f");
                const char* anchItems[]={"Head","Chest"};
                PidoCombo("Dmg anchor","", &g_damageFloaterAnchor, anchItems, 2);
            }
        }
        EndPidoGroup();

        float effectsH = contentH * 0.58f - gap * 0.5f;
        float soundH   = contentH - effectsH - gap;

        ImGui::SetCursorPos({rightX, contentY});
        BeginPidoGroup("##g_effects", "Effects", {childW, effectsH});
        PidoToggle("No flash","", &g_noFlash);
        PidoToggle("No smoke","", &g_noSmoke);
        PidoToggle("Glow","", &g_glowEnabled);
        if(g_glowEnabled){
            PidoColorEdit4("Glow enemy","", g_glowEnemyCol);
            PidoColorEdit4("Glow team","", g_glowTeamCol, ImGuiColorEditFlags_NoAlpha);
            PidoSliderFloat("Glow alpha","", &g_glowAlpha, 0.2f, 1.0f, "%.2f");
        }
        EndPidoGroup();

        ImGui::SetCursorPos({rightX, contentY + effectsH + gap});
        BeginPidoGroup("##g_sound", "Sound", {childW, soundH});
        PidoToggle("Sound pings","", &g_soundEnabled);
        if(g_soundEnabled){
            PidoSliderFloat("Scale","", &g_soundPuddleScale, 0.5f, 2.5f, "%.2f");
            PidoSliderFloat("Alpha","", &g_soundPuddleAlpha, 0.2f, 1.5f, "%.2f");
            PidoToggle("Enemy","", &g_soundBlipEnemy);
            PidoToggle("Teammates","", &g_soundBlipTeam);
            PidoColorEdit4("Color","", g_soundBlipCol);
        }
        EndPidoGroup();
    }else if(g_activeTab==2){
        ImGui::SetCursorPos({contentX, contentY});
        BeginPidoGroup("##g_world", "World", {childW, contentH});
        PidoToggle("Sky color","", &g_skyColorEnabled);
        if(g_skyColorEnabled) PidoColorEdit4("Sky##col","", g_skyColor);
        PidoToggle("World color","Tint map/props (scene draw)", &g_worldColorEnabled);
        if(g_worldColorEnabled) PidoColorEdit4("World tint (multiply)","", g_worldColor);
        EndPidoGroup();

        ImGui::SetCursorPos({rightX, contentY});
        BeginPidoGroup("##g_part", "Particles", {childW, contentH});
        PidoToggle("Snow","", &g_snowEnabled);
        if(g_snowEnabled) PidoSliderInt("Density","", &g_snowDensity, 0, 2);
        PidoToggle("Sakura","", &g_sakuraEnabled);
        if(g_sakuraEnabled) PidoColorEdit4("Sakura","", g_sakuraCol);
        PidoToggle("Stars","", &g_starsEnabled);
        PidoToggle("3D world","", &g_particlesWorld);
        if(g_particlesWorld){
            PidoSliderFloat("World radius","", &g_particlesWorldRadius, 200.f, 2000.f, "%.0f");
            PidoSliderFloat("World height","", &g_particlesWorldHeight, 100.f, 1200.f, "%.0f");
            PidoSliderFloat("World floor","", &g_particlesWorldFloor, -200.f, 200.f, "%.0f");
            PidoSliderFloat("Wind","", &g_particlesWind, 0.f, 60.f, "%.0f");
            PidoSliderFloat("Depth fade","", &g_particlesDepthFade, 0.0005f, 0.01f, "%.4f");
        }
        EndPidoGroup();
    }else if(g_activeTab==3){
        int movRows = 3 + (g_strafeEnabled ? 1 : 0) + (g_dtEnabled ? 1 : 0);
        float movH   = grpH(movRows);
        float radH   = grpH(1);
        float hudH   = contentH - movH - radH - gap * 2.f;
        if (hudH < 40.f * s) hudH = 40.f * s;

        ImGui::SetCursorPos({contentX, contentY});
        BeginPidoGroup("##g_move", "Movement", {childW, movH});
        PidoToggle("Bunny hop","", &g_bhopEnabled);
        PidoToggle("Auto strafe","", &g_strafeEnabled);
        if(g_strafeEnabled) PidoKeybind("Strafe key","", &g_strafeKey);
        PidoToggle("Double tap","", &g_dtEnabled);
        if(g_dtEnabled) PidoKeybind("DT key","", &g_dtKey);
        EndPidoGroup();

        ImGui::SetCursorPos({contentX, contentY + movH + gap});
        BeginPidoGroup("##g_radar", "Radar", {childW, radH});
        PidoToggle("In-game radar","", &g_radarIngame);
        EndPidoGroup();

        ImGui::SetCursorPos({contentX, contentY + movH + radH + gap * 2.f});
        BeginPidoGroup("##g_hud", "HUD", {childW, hudH});
        PidoToggle("Bomb timer","", &g_bombTimerEnabled);
        PidoToggle("Watermark","", &g_watermarkEnabled);
        if(g_watermarkEnabled) PidoToggle("Watermark fps","", &g_showFpsWatermark);
        PidoToggle("Spectator list","", &g_spectatorListEnabled);
        PidoToggle("Keybinds","", &g_keybindsEnabled);
        EndPidoGroup();

        float viewH   = grpH(2);
        float configH = contentH - viewH - gap;
        if (configH < 40.f * s) configH = 40.f * s;

        ImGui::SetCursorPos({rightX, contentY});
        BeginPidoGroup("##g_view", "View", {childW, viewH});
        PidoToggle("FOV changer","", &g_fovEnabled);
        PidoSliderFloat("FOV","", &g_fovValue, 70.f, 130.f, "%.0f");
        EndPidoGroup();

        ImGui::SetCursorPos({rightX, contentY + viewH + gap});
        BeginPidoGroup("##g_config", "Config", {childW, configH});
        PidoSliderFloat("Menu opacity","", &g_menuOpacity, 0.6f, 1.0f, "%.2f");
        PidoSliderFloat("Menu anim","", &g_menuAnimSpeed, 4.f, 20.f, "%.1f");
        PidoSliderFloat("ESP scale","", &g_espScale, 0.7f, 1.5f, "%.2f");
        PidoSliderFloat("Menu scale","", &g_uiScale, 0.85f, 1.6f, "%.2f");
        PidoInputText("Config name","", g_configName, sizeof(g_configName));
        static int g_configListSel = -1;
        if(!g_configList.empty()){
            std::vector<const char*> cfgPtrs;
            cfgPtrs.reserve(g_configList.size());
            for(auto& c : g_configList) cfgPtrs.push_back(c.c_str());
            if(g_configListSel < 0 || g_configListSel >= (int)cfgPtrs.size()) g_configListSel = 0;
            if(PidoCombo("Config list##cfg", "", &g_configListSel, (const char* const*)cfgPtrs.data(), (int)cfgPtrs.size())){
                strncpy_s(g_configName, sizeof(g_configName), g_configList[g_configListSel].c_str(), _TRUNCATE);
            }
        }
        float btnW = (ImGui::GetContentRegionAvail().x - 8.f*s) / 4.f;
        if(PidoButton("Save",    ImVec2(btnW, 0))){ if(SaveConfig(g_configName)) RefreshConfigList(); }
        ImGui::SameLine(0, 4.f*s);
        if(PidoButton("Load",    ImVec2(btnW, 0))) LoadConfig(g_configName);
        ImGui::SameLine(0, 4.f*s);
        if(PidoButton("Refresh", ImVec2(btnW, 0))) RefreshConfigList();
        ImGui::SameLine(0, 4.f*s);
        if(PidoButton("Reset",   ImVec2(btnW, 0))) ApplyDefaults();
        EndPidoGroup();
    }

    ImGui::PopStyleVar();

    if(font::bold) ImGui::PopFont();
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}

static void DrawKeybindsWindow(){
    if(!g_keybindsEnabled) return;

    struct KeybindRow{
        const char* label;
        int key;
        bool active;
        bool always;
    };

    KeybindRow rows[4]{};
    int rowCount = 0;
    auto addRow = [&](bool enabled, const char* label, int key, bool active, bool always){
        if(!enabled) return;
        if(!g_menuOpen && !active) return;
        rows[rowCount++] = {label, key, active, always};
    };

    bool aimActive = g_aimbotEnabled && g_aimbotKey != 0 && (GetAsyncKeyState(g_aimbotKey)&0x8000);
    bool tbActive = g_tbEnabled && (g_tbKey == 0 || (GetAsyncKeyState(g_tbKey)&0x8000));
    bool dtActive = g_dtEnabled && g_dtKey != 0 && (GetAsyncKeyState(g_dtKey)&0x8000);
    bool strafeActive = g_strafeEnabled && (g_strafeKey == 0 || (GetAsyncKeyState(g_strafeKey)&0x8000));

    addRow(g_aimbotEnabled && g_aimbotKey != 0, "Aimbot", g_aimbotKey, aimActive, false);
    addRow(g_tbEnabled, "Triggerbot", g_tbKey, tbActive, g_tbKey == 0);
    addRow(g_dtEnabled && g_dtKey != 0, "Double tap", g_dtKey, dtActive, false);
    addRow(g_strafeEnabled, "Auto strafe", g_strafeKey, strafeActive, g_strafeKey == 0);

    if(rowCount <= 0) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if(!dl) return;

    ImFont* fBold = font::bold ? font::bold : ImGui::GetFont();
    ImFont* fReg = font::regular ? font::regular : ImGui::GetFont();
    const float margin = 12.f;
    const float gap = 6.f;
    const ImU32 colAccent = IM_COL32(220,220,220,255);
    const ImU32 colText = IM_COL32(220,220,220,255);
    const ImU32 colDim = IM_COL32(140,140,140,255);

    char cntBuf[8];
    std::snprintf(cntBuf, sizeof(cntBuf), "%d", rowCount);
    float y = margin;
    std::vector<OverlayBarItem> header{
        {"keybinds", colAccent, true},
        {cntBuf, colText, false},
    };
    ImVec2 headerSize = DrawOverlayWatermarkBar(dl, fBold, fReg, margin, y, header, false, 150.f);
    y += headerSize.y + gap;

    for(int i = 0; i < rowCount; i++){
        std::string state = rows[i].always ? "always" : KeyName(rows[i].key);
        ImU32 stateCol = rows[i].active ? IM_COL32(140,220,160,255) : colDim;
        std::vector<OverlayBarItem> row{
            {rows[i].label, colText, true},
            {state, stateCol, false},
        };
        ImVec2 rowSize = DrawOverlayWatermarkBar(dl, fBold, fReg, margin, y, row, false, 170.f);
        y += rowSize.y + gap;
    }
}
