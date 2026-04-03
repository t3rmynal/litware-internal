// конфиги

static std::string ConfigDir(){
    char buf[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableA("APPDATA", buf, MAX_PATH);
    if(len == 0 || len >= MAX_PATH) return ".";
    std::string dir = std::string(buf) + "\\litware";
    return dir;
}

static std::string ConfigPath(const char* name){
    std::string safe = name && name[0] ? name : "default";
    for(char& c : safe){if(c=='/'||c=='\\'||c==':'||c=='*'||c=='?'||c=='\"'||c=='<'||c=='>'||c=='|')c='_';}
    return ConfigDir() + "\\" + safe + ".cfg";
}

static void RefreshConfigList();

static void EnsureConfigDir(){
    std::error_code ec;
    std::filesystem::create_directories(ConfigDir(), ec);
    RefreshConfigList();
}

static void RefreshConfigList(){
    g_configList.clear();
    std::error_code ec;
    const std::string dir = ConfigDir();
    if(!std::filesystem::exists(dir, ec)) return;
    for(const auto& entry : std::filesystem::directory_iterator(dir, ec)){
        if(entry.is_regular_file()){
            auto path = entry.path();
            if(path.extension() == ".cfg"){
                g_configList.emplace_back(path.stem().string());
            }
        }
    }
    std::sort(g_configList.begin(), g_configList.end());
}

struct WeaponInfo{
    const char* name;
    const char* icon;
    int maxClip;
    ImWchar iconChar;  // 0 если нет иконки
};

static WeaponInfo WeaponInfoForId(int id){
    switch(id){
        case 1: return {"Deagle","DE",7,0xE001};
        case 2: return {"Dualies","DUAL",30,0xE002};
        case 3: return {"Five-SeveN","57",20,0xE003};
        case 4: return {"Glock","GLOCK",20,0xE004};
        case 7: return {"AK-47","AK",30,0xE007};
        case 8: return {"AUG","AUG",30,0xE008};
        case 9: return {"AWP","AWP",10,0xE009};
        case 10: return {"FAMAS","FAMAS",25,0xE00A};
        case 11: return {"G3SG1","G3",20,0xE00B};
        case 13: return {"Galil","GALIL",35,0xE00D};
        case 14: return {"M249","M249",100,0xE00E};
        case 16: return {"M4A4","M4",30,0xE010};
        case 17: return {"MAC-10","MAC10",30,0xE011};
        case 19: return {"P90","P90",50,0xE013};
        case 24: return {"UMP-45","UMP",25,0xE018};
        case 25: return {"XM1014","XM",7,0xE019};
        case 26: return {"Bizon","BIZON",64,0xE01A};
        case 27: return {"MAG-7","MAG7",5,0xE01B};
        case 28: return {"Negev","NEGEV",150,0xE01C};
        case 29: return {"Sawed-Off","SAWED",7,0xE01D};
        case 30: return {"Tec-9","TEC9",18,0xE01E};
        case 32: return {"P2000","P2K",13,0xE020};
        case 33: return {"MP7","MP7",30,0xE021};
        case 34: return {"MP9","MP9",30,0xE022};
        case 35: return {"Nova","NOVA",8,0xE023};
        case 36: return {"P250","P250",13,0xE024};
        case 38: return {"SCAR-20","SCAR",20,0xE026};
        case 39: return {"SG553","SG",30,0xE027};
        case 40: return {"SSG08","SSG",10,0xE028};
        case 42: return {"Knife","KN",0,0xE02A};
        case 59: return {"Knife","KN",0,0xE03B};
        case 60: return {"M4A1-S","M4S",20,0xE03C};
        case 61: return {"USP-S","USP",12,0xE03D};
        case 63: return {"CZ75","CZ75",12,0xE03F};
        case 64: return {"Revolver","R8",8,0xE040};
        case 80: return {"Zeus","ZEU",1,0xE050};
        case 81: return {"Flash","FL",0,0xE051};
        case 82: return {"HE","HE",0,0xE052};
        case 83: return {"Smoke","SM",0,0xE053};
        case 84: return {"Decoy","DEC",0,0xE054};
        case 86: return {"Molotov","MOL",0,0xE056};
        case 87: return {"Incendiary","INC",0,0xE057};
        case 88: return {"C4","C4",0,0xE058};
        default: return {"Weapon","WPN",0,(ImWchar)(id>=1&&id<=255?0xE000u+(unsigned)id:0)};
    }
}

static uintptr_t GetActiveWeapon(uintptr_t pawn, uintptr_t entityList){
    if(!pawn) return 0;
    uintptr_t ws = Rd<uintptr_t>(pawn + offsets::base_pawn::m_pWeaponServices);
    if(!ws) return 0;
    uint32_t hWeapon = Rd<uint32_t>(ws + offsets::weapon_services::m_hActiveWeapon);
    return ResolveHandle(entityList, hWeapon);
}

static int GetWeaponId(uintptr_t weapon){
    if(!weapon) return 0;
    uintptr_t attr = weapon + offsets::econ_entity::m_AttributeManager;
    uintptr_t item = attr + offsets::attribute_container::m_Item;
    return (int)Rd<uint16_t>(item + offsets::econ_item_view::m_iItemDefinitionIndex);
}

static int GetWeaponClip(uintptr_t weapon){
    if(!weapon) return 0;
    return Rd<int>(weapon + offsets::base_weapon::m_iClip1);
}

static bool PlayerHasWeaponId(uintptr_t pawn, uintptr_t entityList, int weaponId){
    if(!pawn||!entityList) return false;
    uintptr_t ws = Rd<uintptr_t>(pawn + offsets::base_pawn::m_pWeaponServices);
    if(!ws) return false;
    uintptr_t vecBase = ws + offsets::weapon_services::m_hMyWeapons;
    int count = Rd<int>(vecBase); if(count<=0||count>16) return false;
    uintptr_t data = Rd<uintptr_t>(vecBase + 8);
    if(!data) return false;
    for(int i=0;i<count;i++){
        uint32_t h = Rd<uint32_t>(data + (size_t)i*4);
        uintptr_t w = ResolveHandle(entityList, h);
        if(w && GetWeaponId(w)==weaponId) return true;
    }
    return false;
}

static SkinOverride* FindSkinOverride(int weaponId){
    for(auto& o : g_skinOverrides){
        if(o.weaponId == weaponId) return &o;
    }
    return nullptr;
}

static void SetSkinOverride(int weaponId, int paintKit, float wear, int seed, int statTrak){
    if(weaponId <= 0) return;
    SkinOverride* o = FindSkinOverride(weaponId);
    if(!o){
        g_skinOverrides.push_back({});
        o = &g_skinOverrides.back();
    }
    o->weaponId = weaponId;
    o->paintKit = paintKit;
    o->wear = Clampf(wear, 0.0001f, 1.f);
    o->seed = seed;
    o->statTrak = statTrak;
}

static void RemoveSkinOverride(int weaponId){
    g_skinOverrides.erase(
        std::remove_if(g_skinOverrides.begin(), g_skinOverrides.end(),
            [&](const SkinOverride& o){ return o.weaponId == weaponId; }),
        g_skinOverrides.end());
}

static void CollectWeapons(uintptr_t pawn, uintptr_t entityList, std::vector<uintptr_t>& out){
    out.clear();
    if(!pawn || !entityList) return;
    uintptr_t ws = Rd<uintptr_t>(pawn + offsets::base_pawn::m_pWeaponServices);
    if(!ws) return;
    uintptr_t vecBase = ws + offsets::weapon_services::m_hMyWeapons;
    int count = Rd<int>(vecBase);
    if(count <= 0 || count > 64) return;
    uintptr_t data = Rd<uintptr_t>(vecBase + 8);
    if(!data) return;
    out.reserve(count);
    for(int i = 0; i < count; i++){
        uint32_t h = Rd<uint32_t>(data + (size_t)i * 4);
        uintptr_t w = ResolveHandle(entityList, h);
        if(w) out.push_back(w);
    }
}

static void ApplySkinToWeapon(uintptr_t weapon, const SkinOverride& o){
    if(!weapon) return;
    uintptr_t attr = weapon + offsets::econ_entity::m_AttributeManager;
    uintptr_t item = attr + offsets::attribute_container::m_Item;
    if(o.paintKit <= 0){
        Wr<int>(weapon + offsets::econ_entity::m_nFallbackPaintKit, -1);
        Wr<int>(weapon + offsets::econ_entity::m_nFallbackSeed, 0);
        Wr<float>(weapon + offsets::econ_entity::m_flFallbackWear, 0.01f);
        Wr<int>(weapon + offsets::econ_entity::m_nFallbackStatTrak, 0);
        return;
    }
    Wr<uint32_t>(item + offsets::econ_item_view::m_iItemIDHigh, 0xFFFFFFFF);
    Wr<uint32_t>(item + offsets::econ_item_view::m_iItemIDLow, 0xFFFFFFFF);
    Wr<int>(weapon + offsets::econ_entity::m_nFallbackPaintKit, o.paintKit);
    Wr<int>(weapon + offsets::econ_entity::m_nFallbackSeed, o.seed);
    Wr<float>(weapon + offsets::econ_entity::m_flFallbackWear, Clampf(o.wear, 0.0001f, 1.f));
    Wr<int>(weapon + offsets::econ_entity::m_nFallbackStatTrak, o.statTrak);
}

static int GetPlayerMoney(uintptr_t controller){
    if(!controller) return 0;
    uintptr_t ms = Rd<uintptr_t>(controller + offsets::controller::m_pInGameMoneyServices);
    if(!ms) return 0;
    return Rd<int>(ms + offsets::money_services::m_iAccount);
}

static void WriteBool(std::ofstream& out, const char* key, bool v){out<<key<<"="<<(v?1:0)<<"\n";}
static void WriteInt(std::ofstream& out, const char* key, int v){out<<key<<"="<<v<<"\n";}
static void WriteFloat(std::ofstream& out, const char* key, float v){out<<key<<"="<<v<<"\n";}
static void WriteColor(std::ofstream& out, const char* key, const float c[4]){
    out<<key<<"="<<c[0]<<","<<c[1]<<","<<c[2]<<","<<c[3]<<"\n";
}
static void WriteColor4(std::ofstream& out, const char* key, const float c[4]){
    WriteColor(out, key, c);
}

static bool ParseBool(const std::string& v){
    return v=="1"||v=="true"||v=="True"||v=="TRUE";
}

static bool ParseInt(const std::string& v, int& out){
    char* end=nullptr;
    long val = std::strtol(v.c_str(), &end, 10);
    if(end==v.c_str()) return false;
    out = (int)val;
    return true;
}

static bool ParseFloat(const std::string& v, float& out){
    char* end=nullptr;
    float val = std::strtof(v.c_str(), &end);
    if(end==v.c_str()) return false;
    out = val;
    return true;
}

static bool ParseColor4(const std::string& v, float out[4]){
    std::stringstream ss(v);
    char c1=0,c2=0,c3=0;
    if(!(ss>>out[0]))return false;
    if(!(ss>>c1))return false;
    if(!(ss>>out[1]))return false;
    if(!(ss>>c2))return false;
    if(!(ss>>out[2]))return false;
    if(!(ss>>c3))return false;
    if(!(ss>>out[3]))return false;
    return true;
}

static bool ParseSkinOverride(const std::string& v, SkinOverride& out){
    std::stringstream ss(v);
    char c=0;
    if(!(ss>>out.weaponId)) return false;
    if(!(ss>>c) || c!=',') return false;
    if(!(ss>>out.paintKit)) return false;
    if(!(ss>>c) || c!=',') return false;
    if(!(ss>>out.wear)) return false;
    if(!(ss>>c) || c!=',') return false;
    if(!(ss>>out.seed)) return false;
    if(!(ss>>c) || c!=',') return false;
    if(!(ss>>out.statTrak)) return false;
    return true;
}

static bool LoadConfigKeyEsp(const std::string& key, const std::string& val, bool& ok){
    if(key=="esp_enabled"){ g_espEnabled=ParseBool(val); return true; }
    if(key=="esp_draw_box"){ g_espDrawBox=ParseBool(val); return true; }
    if(key=="esp_only_vis"){ g_espOnlyVis=ParseBool(val); return true; }
    if(key=="esp_box_style"){ int v; if(ParseInt(val,v)) g_espBoxStyle=v; else ok=false; return true; }
    if(key=="esp_box_thick"){ float v; if(ParseFloat(val,v)) g_espBoxThick=v; else ok=false; return true; }
    if(key=="esp_box_shadow"){ g_espBoxShadow=ParseBool(val); return true; }
    if(key=="esp_enemy_col"){ if(!ParseColor4(val,g_espEnemyCol)) ok=false; return true; }
    if(key=="esp_team_col"){ if(!ParseColor4(val,g_espTeamCol)) ok=false; return true; }
    if(key=="esp_team"){ g_espShowTeam=ParseBool(val); return true; }
    if(key=="esp_name"){ g_espName=ParseBool(val); return true; }
    if(key=="esp_name_size"){ float v; if(ParseFloat(val,v)) g_espNameSize=v; else ok=false; return true; }
    if(key=="esp_scale"){ float v; if(ParseFloat(val,v)) g_espScale=v; else ok=false; return true; }
    if(key=="esp_preview_pos"){ int v; if(ParseInt(val,v)) g_espPreviewPos=v; else ok=false; return true; }
    if(key=="esp_health"){ g_espHealth=ParseBool(val); return true; }
    if(key=="esp_health_pos"){ int v; if(ParseInt(val,v)) g_espHealthPos=v; else ok=false; return true; }
    if(key=="esp_health_style"){ int v; if(ParseInt(val,v)) g_espHealthStyle=v; else ok=false; return true; }
    if(key=="health_grad_col1"){ if(!ParseColor4(val,g_espHealthGradientCol1)) ok=false; return true; }
    if(key=="health_grad_col2"){ if(!ParseColor4(val,g_espHealthGradientCol2)) ok=false; return true; }
    if(key=="esp_ammo_style"){ int v; if(ParseInt(val,v)) g_espAmmoStyle=v; else ok=false; return true; }
    if(key=="esp_ammo_col1"){ if(!ParseColor4(val,g_espAmmoCol1)) ok=false; return true; }
    if(key=="esp_ammo_col2"){ if(!ParseColor4(val,g_espAmmoCol2)) ok=false; return true; }
    if(key=="esp_dist"){ g_espDist=ParseBool(val); return true; }
    if(key=="esp_max_dist"){ float v; if(ParseFloat(val,v)) g_espMaxDist=v; else ok=false; return true; }
    if(key=="esp_skeleton"){ g_espSkeleton=ParseBool(val); return true; }
    if(key=="esp_lines"){ g_espLines=ParseBool(val); return true; }
    if(key=="esp_oof"){ g_espOof=ParseBool(val); return true; }
    if(key=="esp_oof_size"){ float v; if(ParseFloat(val,v)) g_espOofSize=v; else ok=false; return true; }
    if(key=="skeleton_thick"){ float v; if(ParseFloat(val,v)) g_skeletonThick=v; else ok=false; return true; }
    if(key=="esp_head_dot"){ g_espHeadDot=ParseBool(val); return true; }
    if(key=="esp_spotted"){ g_espSpotted=ParseBool(val); return true; }
    if(key=="esp_vis_check"){ g_visCheckEnabled=ParseBool(val); return true; }
    if(key=="esp_weapon"){ g_espWeapon=ParseBool(val); return true; }
    if(key=="esp_weapon_icon"){ g_espWeaponIcon=ParseBool(val); return true; }
    if(key=="esp_ammo"){ g_espAmmo=ParseBool(val); return true; }
    if(key=="esp_money"){ g_espMoney=ParseBool(val); return true; }
    if(key=="esp_head_forward"){ float v; if(ParseFloat(val,v)) g_espHeadForward=v; else ok=false; return true; }
    return false;
}
static bool LoadConfigKeyChams(const std::string& key, const std::string& val, bool& ok){
    if(key=="no_flash"){ g_noFlash=ParseBool(val); return true; }
    if(key=="no_smoke"){ g_noSmoke=ParseBool(val); return true; }
    if(key=="no_crosshair"){ g_noCrosshair=ParseBool(val); return true; }
    if(key=="no_legs"){ g_noLegs=ParseBool(val); return true; }
    if(key=="glow_enabled"){ g_glowEnabled=ParseBool(val); return true; }
    if(key=="glow_enemy_col"){ if(!ParseColor4(val,g_glowEnemyCol)) ok=false; return true; }
    if(key=="glow_team_col"){ if(!ParseColor4(val,g_glowTeamCol)) ok=false; return true; }
    if(key=="glow_alpha"){ float v; if(ParseFloat(val,v)) g_glowAlpha=v; else ok=false; return true; }
    if(key=="chams_enabled"){ g_chamsEnabled=ParseBool(val); return true; }
    if(key=="chams_enemy_only"){ g_chamsEnemyOnly=ParseBool(val); return true; }
    if(key=="chams_ignorez"){ g_chamsIgnoreZ=ParseBool(val); return true; }
    if(key=="chams_material"){ int v; if(ParseInt(val,v)) g_chamsMaterial=v; else ok=false; return true; }
    if(key=="chams_enemy_col"){ if(!ParseColor4(val,g_chamsEnemyCol)) ok=false; return true; }
    if(key=="chams_team_col"){ if(!ParseColor4(val,g_chamsTeamCol)) ok=false; return true; }
    if(key=="chams_ignorez_col"){ if(!ParseColor4(val,g_chamsIgnoreZCol)) ok=false; return true; }
    if(key=="chams_scene"){ g_chamsScene=ParseBool(val); return true; }
    if(key=="weapon_chams"){ g_weaponChamsEnabled=ParseBool(val); return true; }
    if(key=="weapon_chams_col"){ if(!ParseColor4(val,g_weaponChamsCol)) ok=false; return true; }
    return false;
}
static bool LoadConfigKeyAimbot(const std::string& key, const std::string& val, bool& ok, bool& rcsXSet, bool& rcsYSet){
    if(key=="aimbot_enabled"){ g_aimbotEnabled=ParseBool(val); return true; }
    if(key=="aimbot_key"){ int v; if(ParseInt(val,v)) g_aimbotKey=v; else ok=false; return true; }
    if(key=="aimbot_fov"){ float v; if(ParseFloat(val,v)) g_aimbotFov=v; else ok=false; return true; }
    if(key=="aimbot_smooth"){ float v; if(ParseFloat(val,v)) g_aimbotSmooth=v; else ok=false; return true; }
    if(key=="aimbot_bone"){ int v; if(ParseInt(val,v)) g_aimbotBone=v; else ok=false; return true; }
    if(key=="fov_circle"){ g_fovCircleEnabled=ParseBool(val); return true; }
    if(key=="fov_circle_col"){ if(!ParseColor4(val,g_fovCircleCol)) ok=false; return true; }
    if(key=="aimbot_team"){ g_aimbotTeamChk=ParseBool(val); return true; }
    if(key=="aimbot_vis"){ g_aimbotVisCheck=ParseBool(val); return true; }
    if(key=="wait_aim_fire"){ g_waitAimThenFire=ParseBool(val); return true; }
    if(key=="wait_aim_deg"){ float v; if(ParseFloat(val,v)) g_waitAimFovDeg=v; else ok=false; return true; }
    if(key=="autostop"){ g_autostopEnabled=ParseBool(val); return true; }
    if(key=="rcs_enabled"){ g_rcsEnabled=ParseBool(val); return true; }
    if(key=="rcs_x"){ float v; if(ParseFloat(val,v)){ g_rcsX=v; rcsXSet=true; } else ok=false; return true; }
    if(key=="rcs_y"){ float v; if(ParseFloat(val,v)){ g_rcsY=v; rcsYSet=true; } else ok=false; return true; }
    if(key=="rcs_smooth"){ float v; if(ParseFloat(val,v)) g_rcsSmooth=v; else ok=false; return true; }
    if(key=="rcs_strength"){ float v; if(ParseFloat(val,v)){ if(!rcsXSet&&!rcsYSet){ g_rcsX=v; g_rcsY=v; } } else ok=false; return true; }
    if(key=="tb_enabled"){ g_tbEnabled=ParseBool(val); return true; }
    if(key=="tb_key"){ int v; if(ParseInt(val,v)) g_tbKey=v; else ok=false; return true; }
    if(key=="tb_delay"){ int v; if(ParseInt(val,v)) g_tbDelay=v; else ok=false; return true; }
    if(key=="tb_team"){ g_tbTeamChk=ParseBool(val); return true; }
    if(key=="dt_enabled"){ g_dtEnabled=ParseBool(val); return true; }
    if(key=="dt_key"){ int v; if(ParseInt(val,v)) g_dtKey=v; else ok=false; return true; }
    return false;
}
static bool LoadConfigKeyMovement(const std::string& key, const std::string& val, bool& ok){
    if(key=="bhop"){ g_bhopEnabled=ParseBool(val); return true; }
    if(key=="strafe_enabled"){ g_strafeEnabled=ParseBool(val); return true; }
    if(key=="strafe_key"){ int v; if(ParseInt(val,v)) g_strafeKey=v; else ok=false; return true; }
    if(key=="night_mode_overlay"){ g_nightModeOverlay=ParseBool(val); return true; }
    if(key=="fov_enabled"){ g_fovEnabled=ParseBool(val); return true; }
    if(key=="fov_value"){ float v; if(ParseFloat(val,v)) g_fovValue=v; else ok=false; return true; }
    return false;
}
static bool LoadConfigKeyVisual(const std::string& key, const std::string& val, bool& ok){
    if(key=="hands_color_enabled"){ g_handsColorEnabled=ParseBool(val); return true; }
    if(key=="hands_color"){ if(!ParseColor4(val,g_handsColor)) ok=false; return true; }
    if(key=="snow"){ g_snowEnabled=ParseBool(val); return true; }
    if(key=="snow_density"){ int v; if(ParseInt(val,v)) g_snowDensity=v; else ok=false; return true; }
    if(key=="sakura"){ g_sakuraEnabled=ParseBool(val); return true; }
    if(key=="sakura_col"){ if(!ParseColor4(val,g_sakuraCol)) ok=false; return true; }
    if(key=="stars"){ g_starsEnabled=ParseBool(val); return true; }
    if(key=="kill_effect"){ g_killEffectEnabled=ParseBool(val); return true; }
    if(key=="particles_world"){ g_particlesWorld=ParseBool(val); return true; }
    if(key=="particles_world_radius"){ float v; if(ParseFloat(val,v)) g_particlesWorldRadius=v; else ok=false; return true; }
    if(key=="particles_world_height"){ float v; if(ParseFloat(val,v)) g_particlesWorldHeight=v; else ok=false; return true; }
    if(key=="particles_world_floor"){ float v; if(ParseFloat(val,v)) g_particlesWorldFloor=v; else ok=false; return true; }
    if(key=="particles_wind"){ float v; if(ParseFloat(val,v)) g_particlesWind=v; else ok=false; return true; }
    if(key=="particles_depth_fade"){ float v; if(ParseFloat(val,v)) g_particlesDepthFade=v; else ok=false; return true; }
    if(key=="sky_color_enabled"){ g_skyColorEnabled=ParseBool(val); return true; }
    if(key=="sky_color"){ if(!ParseColor4(val,g_skyColor)) ok=false; return true; }
    if(key=="world_color_enabled") return true;
    if(key=="world_color") return true;
    if(key=="damage_floaters"){ g_damageFloatersEnabled=ParseBool(val); return true; }
    if(key=="damage_floater_duration"){ float v; if(ParseFloat(val,v)) g_damageFloaterDuration=v; else ok=false; return true; }
    if(key=="damage_floater_scale"){ float v; if(ParseFloat(val,v)) g_damageFloaterScale=v; else ok=false; return true; }
    if(key=="damage_floater_anchor"){ int v; if(ParseInt(val,v)) g_damageFloaterAnchor=v; else ok=false; return true; }
    return false;
}
static bool LoadConfigKeySkins(const std::string& key, const std::string& val, bool& ok){
    if(key=="skin_enabled"){ g_skinEnabled=ParseBool(val); return true; }
    if(key=="skin_active_only"){ g_skinActiveOnly=ParseBool(val); return true; }
    if(key=="skin_clear"){ g_skinOverrides.clear(); return true; }
    if(key.rfind("skin_override_", 0) == 0){
        SkinOverride o{};
        if(ParseSkinOverride(val, o)){
            SetSkinOverride(o.weaponId, o.paintKit, o.wear, o.seed, o.statTrak);
        }else{
            ok = false;
        }
        return true;
    }
    return false;
}
static bool LoadConfigKeyMisc(const std::string& key, const std::string& val, bool& ok){
    if(key=="watermark"){ g_watermarkEnabled=ParseBool(val); return true; }
    if(key=="watermark_fps"){ g_showFpsWatermark=ParseBool(val); return true; }
        if(key=="spectator_list"){ g_spectatorListEnabled=ParseBool(val); return true; }
        if(key=="keybinds_enabled"){ g_keybindsEnabled=ParseBool(val); return true; }
    if(key=="hit_notif"){ g_hitNotifEnabled=ParseBool(val); return true; }
    if(key=="kill_notif"){ g_killNotifEnabled=ParseBool(val); return true; }
    if(key=="hit_sound"){ g_hitSoundEnabled=ParseBool(val); return true; }
    if(key=="hit_sound_type"){ int v; if(ParseInt(val,v)) g_hitSoundType=v; else ok=false; return true; }
    if(key=="radar"){ g_radarEnabled=ParseBool(val); return true; }
    if(key=="radar_ingame"){ g_radarIngame=ParseBool(val); return true; }
    if(key=="radar_range"){ float v; if(ParseFloat(val,v)) g_radarRange=v; else ok=false; return true; }
    if(key=="radar_size"){ float v; if(ParseFloat(val,v)) g_radarSize=v; else ok=false; return true; }
    if(key=="bomb_timer"){ g_bombTimerEnabled=ParseBool(val); return true; }
    if(key=="bullet_trace"){ g_bulletTraceEnabled=ParseBool(val); return true; }
    if(key=="impact_col"){ if(!ParseColor4(val,g_impactCol)) ok=false; return true; }
    if(key=="sound_indicators"){ g_soundEnabled=ParseBool(val); return true; }
    if(key=="sound_puddle_scale"){ float v; if(ParseFloat(val,v)) g_soundPuddleScale=v; else ok=false; return true; }
    if(key=="sound_puddle_alpha"){ float v; if(ParseFloat(val,v)) g_soundPuddleAlpha=v; else ok=false; return true; }
    if(key=="sound_blip_enemy"){ g_soundBlipEnemy=ParseBool(val); return true; }
    if(key=="sound_blip_team"){ g_soundBlipTeam=ParseBool(val); return true; }
    if(key=="sound_blip_col"){ if(!ParseColor4(val,g_soundBlipCol)) ok=false; return true; }
    if(key=="accent"){ if(!ParseColor4(val,g_accentColor)) ok=false; return true; }
    if(key=="menu_opacity"){ float v; if(ParseFloat(val,v)) g_menuOpacity=v; else ok=false; return true; }
    if(key=="ui_scale"){ float v; if(ParseFloat(val,v)) g_uiScale=v; else ok=false; return true; }
    if(key=="menu_anim_speed"){ float v; if(ParseFloat(val,v)) g_menuAnimSpeed=v; else ok=false; return true; }
    return false;
}

static void ApplyDefaults(){
    g_espEnabled = true;
    g_espDrawBox = true;
    g_espOnlyVis = false;
    g_espBoxStyle = 0;
    g_espBoxThick = 1.5f;
    g_espBoxShadow = false;
    g_espEnemyCol[0]=1.f; g_espEnemyCol[1]=0.25f; g_espEnemyCol[2]=0.25f; g_espEnemyCol[3]=1.f;
    g_espTeamCol[0]=0.25f; g_espTeamCol[1]=0.55f; g_espTeamCol[2]=1.f; g_espTeamCol[3]=1.f;
    g_espShowTeam = true;
    g_espName = true;
    g_espNameSize = 13.5f;
    g_espScale = 1.0f;
    g_espHealth = true;
    g_espHealthPos = 0;
    g_espHealthStyle = 0;
    g_espHealthGradientCol1[0]=0.2f; g_espHealthGradientCol1[1]=0.92f; g_espHealthGradientCol1[2]=0.51f; g_espHealthGradientCol1[3]=1.f;
    g_espHealthGradientCol2[0]=1.f; g_espHealthGradientCol2[1]=0.27f; g_espHealthGradientCol2[2]=0.27f; g_espHealthGradientCol2[3]=1.f;
    g_espAmmoStyle = 0;
    g_espAmmoCol1[0]=0.03f; g_espAmmoCol1[1]=0.03f; g_espAmmoCol1[2]=0.05f; g_espAmmoCol1[3]=1.f;
    g_espAmmoCol2[0]=0.35f; g_espAmmoCol2[1]=0.71f; g_espAmmoCol2[2]=1.f; g_espAmmoCol2[3]=1.f;
    g_espDist = true;
    g_espMaxDist = 100.f;
    g_espSkeleton = false;
    g_espLines = false;
    g_espOof = false;
    g_espOofSize = 24.f;
    g_skeletonThick = 1.1f;
    g_espHeadDot = true;
    g_espSpotted = true;
    g_visCheckEnabled = true;
    g_espWeapon = true;
    g_espWeaponIcon = true;
    g_espAmmo = true;
    g_espMoney = true;
    g_espMoneyPos = 0;
    g_espHeadForward = 6.f;
    g_noFlash = false;
    g_noSmoke = false;
    g_noCrosshair = false;
    g_noLegs = false;
    g_glowEnabled = false;
    g_glowEnemyCol[0]=1.f; g_glowEnemyCol[1]=0.18f; g_glowEnemyCol[2]=0.18f; g_glowEnemyCol[3]=1.f;
    g_glowTeamCol[0]=0.f; g_glowTeamCol[1]=0.f; g_glowTeamCol[2]=0.f; g_glowTeamCol[3]=1.f;
    g_glowAlpha = 1.0f;
    g_chamsEnabled = false;
    g_chamsEnemyOnly = true;
    g_chamsIgnoreZ = false;
    g_chamsMaterial = 0;
    g_chamsEnemyCol[0]=1.f; g_chamsEnemyCol[1]=0.2f; g_chamsEnemyCol[2]=0.2f; g_chamsEnemyCol[3]=1.f;
    g_chamsTeamCol[0]=0.2f; g_chamsTeamCol[1]=0.5f; g_chamsTeamCol[2]=1.f; g_chamsTeamCol[3]=1.f;
    g_chamsIgnoreZCol[0]=1.f; g_chamsIgnoreZCol[1]=0.4f; g_chamsIgnoreZCol[2]=0.9f; g_chamsIgnoreZCol[3]=0.6f;
    g_chamsScene = true;
    g_weaponChamsEnabled = false;
    g_weaponChamsCol[0]=1.f; g_weaponChamsCol[1]=0.88f; g_weaponChamsCol[2]=0.35f; g_weaponChamsCol[3]=1.f;
    g_aimbotEnabled = false;
    g_aimbotKey = VK_LBUTTON;
    g_aimbotFov = 5.f;
    g_aimbotSmooth = 6.f;
    g_aimbotBone = 0;
    g_fovCircleEnabled = false;
    g_fovCircleCol[0]=0.4f; g_fovCircleCol[1]=0.7f; g_fovCircleCol[2]=1.f; g_fovCircleCol[3]=0.5f;
    g_aimbotTeamChk = true;
    g_aimbotVisCheck = false;
    g_rcsEnabled = false;
    g_rcsX = 1.0f;
    g_rcsY = 1.0f;
    g_rcsSmooth = 1.0f;
    g_tbEnabled = false;
    g_tbKey = 0;
    g_tbDelay = 50;
    g_tbTeamChk = true;
    g_dtEnabled = false;
    g_dtKey = 0;
    g_bhopEnabled = false;
    g_strafeEnabled = false;
    g_strafeKey = 0;
    g_nightModeOverlay = false;
    g_fovEnabled = false;
    g_fovValue = 90.f;
    g_snowEnabled = false;
    g_snowDensity = 1;
    g_sakuraEnabled = false;
    g_sakuraCol[0]=1.f; g_sakuraCol[1]=0.55f; g_sakuraCol[2]=0.7f; g_sakuraCol[3]=0.85f;
    g_starsEnabled = false;
    g_killEffectEnabled = false;
    g_particlesWorld = true;
    g_particlesWorldRadius = 600.f;
    g_particlesWorldHeight = 320.f;
    g_particlesWorldFloor = 40.f;
    g_particlesWind = 18.f;
    g_particlesDepthFade = 0.0022f;
    g_skyColorEnabled = false;
    g_skyColor[0]=0.4f; g_skyColor[1]=0.5f; g_skyColor[2]=0.8f; g_skyColor[3]=1.f;
    g_damageFloatersEnabled = true;
    g_damageFloaterDuration = 0.85f;
    g_damageFloaterScale = 1.f;
    g_damageFloaterAnchor = 0;
    g_handsColorEnabled = false;
    g_handsColor[0]=0.9f; g_handsColor[1]=0.9f; g_handsColor[2]=0.95f; g_handsColor[3]=1.f;
    g_watermarkEnabled = true;
    g_showFpsWatermark = true;
    g_spectatorListEnabled = true;
    g_keybindsEnabled = true;
    g_hitNotifEnabled = true;
    g_killNotifEnabled = true;
    g_waitAimThenFire = true;
    g_waitAimFovDeg = 2.5f;
    g_autostopEnabled = true;
    g_hitSoundEnabled = false;
    g_hitSoundType = 1;
    g_radarEnabled = true;
    g_radarIngame = false;
    g_radarRange = 2000.f;
    g_radarSize = 180.f;
    g_bombTimerEnabled = true;
    g_bulletTraceEnabled = true;
    g_impactCol[0]=0.35f; g_impactCol[1]=0.94f; g_impactCol[2]=0.47f; g_impactCol[3]=0.78f;
    g_soundEnabled = true;
    g_soundPuddleScale = 1.0f;
    g_soundPuddleAlpha = 1.0f;
    g_soundBlipEnemy = true;
    g_soundBlipTeam = false;
    g_soundBlipCol[0]=1.f; g_soundBlipCol[1]=0.f; g_soundBlipCol[2]=0.f; g_soundBlipCol[3]=1.f;
    g_accentColor[0]=0.1f; g_accentColor[1]=0.55f; g_accentColor[2]=1.0f; g_accentColor[3]=1.0f;
    g_menuOpacity = 1.0f;
    g_uiScale = 1.10f;
    g_menuAnimSpeed = 12.f;
    g_skinEnabled = false;
    g_skinActiveOnly = false;
    g_skinForceUpdate = false;
    g_skinSelectedWeapon = 0;
    g_skinPaintKit = 0;
    g_skinWear = 0.01f;
    g_skinSeed = 0;
    g_skinStatTrak = 0;
    g_skinOverrides.clear();
}

static bool SaveConfig(const char* name){
    EnsureConfigDir();
    std::ofstream out(ConfigPath(name), std::ios::trunc);
    if(!out.is_open()) return false;
    WriteBool(out, "esp_enabled", g_espEnabled);
    WriteBool(out, "esp_draw_box", g_espDrawBox);
    WriteBool(out, "esp_only_vis", g_espOnlyVis);
    WriteInt(out, "esp_box_style", g_espBoxStyle);
    WriteFloat(out, "esp_box_thick", g_espBoxThick);
    WriteBool(out, "esp_box_shadow", g_espBoxShadow);
    WriteColor(out, "esp_enemy_col", g_espEnemyCol);
    WriteColor(out, "esp_team_col", g_espTeamCol);
    WriteBool(out, "esp_team", g_espShowTeam);
    WriteBool(out, "esp_name", g_espName);
    WriteFloat(out, "esp_name_size", g_espNameSize);
    WriteFloat(out, "esp_scale", g_espScale);
    WriteInt(out, "esp_preview_pos", g_espPreviewPos);
    WriteBool(out, "esp_health", g_espHealth);
    WriteInt(out, "esp_health_pos", g_espHealthPos);
    WriteInt(out, "esp_health_style", g_espHealthStyle);
    WriteColor(out, "health_grad_col1", g_espHealthGradientCol1);
    WriteColor(out, "health_grad_col2", g_espHealthGradientCol2);
    WriteInt(out, "esp_ammo_style", g_espAmmoStyle);
    WriteColor(out, "esp_ammo_col1", g_espAmmoCol1);
    WriteColor(out, "esp_ammo_col2", g_espAmmoCol2);
    WriteBool(out, "esp_dist", g_espDist);
    WriteFloat(out, "esp_max_dist", g_espMaxDist);
    WriteBool(out, "esp_skeleton", g_espSkeleton);
    WriteBool(out, "esp_lines", g_espLines);
    WriteBool(out, "esp_oof", g_espOof);
    WriteFloat(out, "esp_oof_size", g_espOofSize);
    WriteFloat(out, "skeleton_thick", g_skeletonThick);
    WriteBool(out, "esp_head_dot", g_espHeadDot);
    WriteBool(out, "esp_spotted", g_espSpotted);
    WriteBool(out, "esp_vis_check", g_visCheckEnabled);
    WriteBool(out, "esp_weapon", g_espWeapon);
    WriteBool(out, "esp_weapon_icon", g_espWeaponIcon);
    WriteBool(out, "esp_ammo", g_espAmmo);
    WriteBool(out, "esp_money", g_espMoney);
    WriteFloat(out, "esp_head_forward", g_espHeadForward);
    WriteBool(out, "no_flash", g_noFlash);
    WriteBool(out, "no_smoke", g_noSmoke);
    WriteBool(out, "no_crosshair", g_noCrosshair);
    WriteBool(out, "no_legs", g_noLegs);
    WriteBool(out, "glow_enabled", g_glowEnabled);
    WriteColor(out, "glow_enemy_col", g_glowEnemyCol);
    WriteColor(out, "glow_team_col", g_glowTeamCol);
    WriteFloat(out, "glow_alpha", g_glowAlpha);
    WriteBool(out, "chams_enabled", g_chamsEnabled);
    WriteBool(out, "chams_enemy_only", g_chamsEnemyOnly);
    WriteBool(out, "chams_ignorez", g_chamsIgnoreZ);
    WriteInt(out, "chams_material", g_chamsMaterial);
    WriteColor(out, "chams_enemy_col", g_chamsEnemyCol);
    WriteColor(out, "chams_team_col", g_chamsTeamCol);
    WriteColor(out, "chams_ignorez_col", g_chamsIgnoreZCol);
    WriteBool(out, "chams_scene", g_chamsScene);
    WriteBool(out, "weapon_chams", g_weaponChamsEnabled);
    WriteColor(out, "weapon_chams_col", g_weaponChamsCol);
    WriteBool(out, "aimbot_enabled", g_aimbotEnabled);
    WriteInt(out, "aimbot_key", g_aimbotKey);
    WriteFloat(out, "aimbot_fov", g_aimbotFov);
    WriteFloat(out, "aimbot_smooth", g_aimbotSmooth);
    WriteInt(out, "aimbot_bone", g_aimbotBone);
    WriteBool(out, "aimbot_team", g_aimbotTeamChk);
    WriteBool(out, "aimbot_vis", g_aimbotVisCheck);

    WriteBool(out, "wait_aim_fire", g_waitAimThenFire);
    WriteFloat(out, "wait_aim_deg", g_waitAimFovDeg);
    WriteBool(out, "autostop", g_autostopEnabled);
    WriteBool(out, "rcs_enabled", g_rcsEnabled);
    WriteFloat(out, "rcs_x", g_rcsX);
    WriteFloat(out, "rcs_y", g_rcsY);
    if(fabsf(g_rcsX - g_rcsY) < 0.0001f) WriteFloat(out, "rcs_strength", g_rcsX);
    WriteFloat(out, "rcs_smooth", g_rcsSmooth);
    WriteBool(out, "tb_enabled", g_tbEnabled);
    WriteInt(out, "tb_key", g_tbKey);
    WriteInt(out, "tb_delay", g_tbDelay);
    WriteBool(out, "tb_team", g_tbTeamChk);
    WriteBool(out, "dt_enabled", g_dtEnabled);
    WriteInt(out, "dt_key", g_dtKey);
    WriteBool(out, "bhop", g_bhopEnabled);
    WriteBool(out, "strafe_enabled", g_strafeEnabled);
    WriteInt(out, "strafe_key", g_strafeKey);
    WriteBool(out, "night_mode_overlay", g_nightModeOverlay);
    WriteBool(out, "fov_enabled", g_fovEnabled);
    WriteFloat(out, "fov_value", g_fovValue);
    WriteBool(out, "fov_circle", g_fovCircleEnabled);
    WriteColor(out, "fov_circle_col", g_fovCircleCol);
    WriteBool(out, "hands_color_enabled", g_handsColorEnabled);
    WriteColor(out, "hands_color", g_handsColor);
    WriteBool(out, "snow", g_snowEnabled);
    WriteInt(out, "snow_density", g_snowDensity);
    WriteBool(out, "sakura", g_sakuraEnabled);
    WriteColor(out, "sakura_col", g_sakuraCol);
    WriteBool(out, "stars", g_starsEnabled);
    WriteBool(out, "kill_effect", g_killEffectEnabled);
    WriteBool(out, "particles_world", g_particlesWorld);
    WriteFloat(out, "particles_world_radius", g_particlesWorldRadius);
    WriteFloat(out, "particles_world_height", g_particlesWorldHeight);
    WriteFloat(out, "particles_world_floor", g_particlesWorldFloor);
    WriteFloat(out, "particles_wind", g_particlesWind);
    WriteFloat(out, "particles_depth_fade", g_particlesDepthFade);
    WriteBool(out, "sky_color_enabled", g_skyColorEnabled);
    WriteColor(out, "sky_color", g_skyColor);
    WriteBool(out, "damage_floaters", g_damageFloatersEnabled);
    WriteFloat(out, "damage_floater_duration", g_damageFloaterDuration);
    WriteFloat(out, "damage_floater_scale", g_damageFloaterScale);
    WriteInt(out, "damage_floater_anchor", g_damageFloaterAnchor);
    WriteBool(out, "watermark", g_watermarkEnabled);
    WriteBool(out, "watermark_fps", g_showFpsWatermark);
    WriteBool(out, "spectator_list", g_spectatorListEnabled);
    WriteBool(out, "keybinds_enabled", g_keybindsEnabled);
    WriteBool(out, "hit_notif", g_hitNotifEnabled);
    WriteBool(out, "kill_notif", g_killNotifEnabled);
    WriteBool(out, "hit_sound", g_hitSoundEnabled);
    WriteInt(out, "hit_sound_type", g_hitSoundType);
    WriteBool(out, "radar", g_radarEnabled);
    WriteBool(out, "radar_ingame", g_radarIngame);
    WriteFloat(out, "radar_range", g_radarRange);
    WriteFloat(out, "radar_size", g_radarSize);
    WriteBool(out, "bomb_timer", g_bombTimerEnabled);
    WriteBool(out, "bullet_trace", g_bulletTraceEnabled);
    WriteColor(out, "impact_col", g_impactCol);
    WriteBool(out, "sound_indicators", g_soundEnabled);
    WriteFloat(out, "sound_puddle_scale", g_soundPuddleScale);
    WriteFloat(out, "sound_puddle_alpha", g_soundPuddleAlpha);
    WriteBool(out, "sound_blip_enemy", g_soundBlipEnemy);
    WriteBool(out, "sound_blip_team", g_soundBlipTeam);
    WriteColor4(out, "sound_blip_col", g_soundBlipCol);
    WriteColor(out, "accent", g_accentColor);
    WriteFloat(out, "menu_opacity", g_menuOpacity);
    WriteFloat(out, "ui_scale", g_uiScale);
    WriteFloat(out, "menu_anim_speed", g_menuAnimSpeed);
    WriteBool(out, "skin_enabled", g_skinEnabled);
    WriteBool(out, "skin_active_only", g_skinActiveOnly);
    out << "skin_clear=1\n";
    for(size_t i = 0; i < g_skinOverrides.size(); ++i){
        const SkinOverride& o = g_skinOverrides[i];
        out << "skin_override_" << i << "="
            << o.weaponId << "," << o.paintKit << "," << o.wear << "," << o.seed << "," << o.statTrak << "\n";
    }
    return true;
}

static bool LoadConfig(const char* name){
    std::ifstream in(ConfigPath(name));
    if(!in.is_open()) return false;
    ApplyDefaults();
    std::string line;
    bool ok = true;
    bool rcsXSet = false;
    bool rcsYSet = false;
    while(std::getline(in, line)){
        if(line.empty()) continue;
        const auto pos = line.find('=');
        if(pos == std::string::npos) continue;
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);
        if(LoadConfigKeyEsp(key, val, ok)) continue;
        if(LoadConfigKeyChams(key, val, ok)) continue;
        if(LoadConfigKeyAimbot(key, val, ok, rcsXSet, rcsYSet)) continue;
        if(LoadConfigKeyMovement(key, val, ok)) continue;
        if(LoadConfigKeyVisual(key, val, ok)) continue;
        if(LoadConfigKeySkins(key, val, ok)) continue;
        LoadConfigKeyMisc(key, val, ok);
    }
    g_menuOpacity = Clampf(g_menuOpacity, 0.3f, 1.0f);
    g_uiScale = Clampf(g_uiScale, 0.85f, 1.6f);
    g_menuAnimSpeed = Clampf(g_menuAnimSpeed, 2.f, 30.f);
    g_glowAlpha = Clampf(g_glowAlpha, 0.f, 1.f);
    g_skeletonThick = Clampf(g_skeletonThick, 0.5f, 3.5f);
    g_espOofSize = Clampf(g_espOofSize, 8.f, 64.f);
    g_particlesWorldRadius = Clampf(g_particlesWorldRadius, 200.f, 2000.f);
    g_particlesWorldHeight = Clampf(g_particlesWorldHeight, 100.f, 1200.f);
    g_particlesWorldFloor = Clampf(g_particlesWorldFloor, -200.f, 400.f);
    g_particlesWind = Clampf(g_particlesWind, 0.f, 60.f);
    g_particlesDepthFade = Clampf(g_particlesDepthFade, 0.0005f, 0.01f);
    g_waitAimFovDeg = Clampf(g_waitAimFovDeg, 0.4f, 15.f);
    g_rcsSmooth = Clampf(g_rcsSmooth, 1.f, 30.f);
    if(g_aimbotBone < 0 || g_aimbotBone > 4) g_aimbotBone = 0;
    g_damageFloaterDuration = Clampf(g_damageFloaterDuration, 0.25f, 2.5f);
    g_damageFloaterScale = Clampf(g_damageFloaterScale, 0.4f, 2.5f);
    g_damageFloaterAnchor &= 1;
    g_soundPuddleScale = Clampf(g_soundPuddleScale, 0.3f, 3.0f);
    g_soundPuddleAlpha = Clampf(g_soundPuddleAlpha, 0.f, 2.0f);
    return ok;
}
