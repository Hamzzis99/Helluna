import unreal

EAL = unreal.EditorAssetLibrary
AF = unreal.AssetToolsHelpers.get_asset_tools()

def save(asset):
    asset.modify()
    EAL.save_loaded_asset(asset)

PISTOL_PATH = "/Game/Hero/Weapon/Gun/BP_HeroWeapon_Pistol"
SHOTGUN_PATH = "/Game/Hero/Weapon/Gun/BP_HeroWeapon_Shotgun"
CURVE_DIR = "/Game/Gihyeon/Combat/Parry/Curves"
VFX_DIR = "/Game/Gihyeon/Combat/Parry"
SHAKE_DIR = "/Game/Gihyeon/Combat/Parry"

print("=" * 60)
print("  건패링 에셋 자동 생성 스크립트")
print("=" * 60)

print("\n[1/5] CurveFloat 에셋 생성...")
curve_factory = unreal.CurveFloatFactory()

curve_configs = {
    "CF_Pistol_ArmLength": [(0.0, 1.0), (0.3, 0.7), (0.7, 0.85), (1.0, 1.0)],
    "CF_Pistol_FOV": [(0.0, 1.0), (0.2, 0.85), (0.5, 0.9), (1.0, 1.0)],
}

for name, keys in curve_configs.items():
    existing = EAL.does_asset_exist(f"{CURVE_DIR}/{name}")
    if existing:
        print(f"  {name}: 이미 존재 -> 스킵")
        continue
    curve_asset = AF.create_asset(name, CURVE_DIR, unreal.CurveFloat, curve_factory)
    if curve_asset:
        rich_curve = curve_asset.get_editor_property("float_curve")
        for time_val, value in keys:
            key = unreal.RichCurveKey()
            key.time = time_val
            key.value = value
            key.interp_mode = unreal.RichCurveInterpMode.RCIM_CUBIC
            key.tangent_mode = unreal.RichCurveTangentMode.RCTM_AUTO
            rich_curve.add_key(time_val, value)
        save(curve_asset)
        print(f"  {name}: 생성 완료 ({len(keys)}개 키)")
    else:
        print(f"  {name}: 생성 실패!")

print("\n[2/5] 카메라 셰이크 BP 생성...")
shake_name = "CS_ParryWarp"
shake_path = f"{SHAKE_DIR}/{shake_name}"
if EAL.does_asset_exist(shake_path):
    print(f"  {shake_name}: 이미 존재 -> 스킵")
else:
    shake_factory = unreal.BlueprintFactory()
    shake_factory.set_editor_property("parent_class", unreal.MatineeCameraShake)
    shake_bp = AF.create_asset(shake_name, SHAKE_DIR, unreal.Blueprint, shake_factory)
    if shake_bp:
        cdo = unreal.get_default_object(shake_bp.generated_class())
        osc = cdo.get_editor_property("oscillation_params")
        loc_osc = osc.get_editor_property("loc_oscillation")
        x_osc = loc_osc.get_editor_property("x")
        x_osc.set_editor_property("amplitude", 3.0)
        x_osc.set_editor_property("frequency", 15.0)
        y_osc = loc_osc.get_editor_property("y")
        y_osc.set_editor_property("amplitude", 2.0)
        y_osc.set_editor_property("frequency", 12.0)
        z_osc = loc_osc.get_editor_property("z")
        z_osc.set_editor_property("amplitude", 2.0)
        z_osc.set_editor_property("frequency", 10.0)
        rot_osc = osc.get_editor_property("rot_oscillation")
        pitch = rot_osc.get_editor_property("pitch")
        pitch.set_editor_property("amplitude", 1.5)
        pitch.set_editor_property("frequency", 20.0)
        yaw = rot_osc.get_editor_property("yaw")
        yaw.set_editor_property("amplitude", 1.0)
        yaw.set_editor_property("frequency", 18.0)
        osc.set_editor_property("duration", 0.15)
        osc.set_editor_property("blend_in_time", 0.02)
        osc.set_editor_property("blend_out_time", 0.08)
        cdo.modify()
        EAL.save_loaded_asset(shake_bp)
        print(f"  {shake_name}: 생성 완료 (Duration=0.15, LocAmp=3/2/2, RotAmp=1.5/1)")
    else:
        print(f"  {shake_name}: 생성 실패!")

print("\n[3/5] WBP_BloodSplash 위젯 생성...")
splash_name = "WBP_BloodSplash"
splash_path = f"{VFX_DIR}/{splash_name}"
if EAL.does_asset_exist(splash_path):
    print(f"  {splash_name}: 이미 존재 -> 스킵")
else:
    widget_factory = unreal.BlueprintFactory()
    widget_factory.set_editor_property("parent_class", unreal.UserWidget)
    splash_bp = AF.create_asset(splash_name, VFX_DIR, unreal.Blueprint, widget_factory)
    if splash_bp:
        EAL.save_loaded_asset(splash_bp)
        print(f"  {splash_name}: 생성 완료 (빈 위젯 — 에디터에서 빨간 비네팅 이미지 추가 필요)")
    else:
        print(f"  {splash_name}: 생성 실패!")

print("\n[4/5] WBP_KickPrompt 위젯 생성...")
kick_name = "WBP_KickPrompt"
kick_path = f"{VFX_DIR}/{kick_name}"
if EAL.does_asset_exist(kick_path):
    print(f"  {kick_name}: 이미 존재 -> 스킵")
else:
    widget_factory2 = unreal.BlueprintFactory()
    widget_factory2.set_editor_property("parent_class", unreal.UserWidget)
    kick_bp = AF.create_asset(kick_name, VFX_DIR, unreal.Blueprint, widget_factory2)
    if kick_bp:
        EAL.save_loaded_asset(kick_bp)
        print(f"  {kick_name}: 생성 완료 (빈 위젯 — 에디터에서 [F] 발차기 프롬프트 UI 추가 필요)")
    else:
        print(f"  {kick_name}: 생성 실패!")

print("\n[5/5] Pistol/Shotgun BP에 에셋 세팅...")

pistol_bp = unreal.load_asset(PISTOL_PATH)
if pistol_bp:
    pistol_cdo = unreal.get_default_object(pistol_bp.generated_class())
    
    arm_curve = unreal.load_asset(f"{CURVE_DIR}/CF_Pistol_ArmLength")
    fov_curve = unreal.load_asset(f"{CURVE_DIR}/CF_Pistol_FOV")
    warp_shake = unreal.load_asset(f"{SHAKE_DIR}/CS_ParryWarp")
    splash_class = unreal.load_asset(f"{VFX_DIR}/WBP_BloodSplash")
    
    if arm_curve:
        pistol_cdo.set_editor_property("parry_camera_arm_curve", arm_curve)
        print(f"  Pistol.ParryCameraArmCurve = CF_Pistol_ArmLength")
    if fov_curve:
        pistol_cdo.set_editor_property("parry_camera_fov_curve", fov_curve)
        print(f"  Pistol.ParryCameraFOVCurve = CF_Pistol_FOV")
    if warp_shake:
        pistol_cdo.set_editor_property("parry_warp_camera_shake", warp_shake.generated_class())
        print(f"  Pistol.ParryWarpCameraShake = CS_ParryWarp")
    if splash_class:
        pistol_cdo.set_editor_property("parry_blood_splash_widget_class", splash_class.generated_class())
        pistol_cdo.set_editor_property("parry_blood_splash_duration", 0.5)
        print(f"  Pistol.ParryBloodSplashWidgetClass = WBP_BloodSplash (Duration=0.5)")
    
    pistol_cdo.modify()
    EAL.save_loaded_asset(pistol_bp)
    print(f"  Pistol BP 저장 완료!")
else:
    print("  Pistol BP 로드 실패!")

shotgun_bp = unreal.load_asset(SHOTGUN_PATH)
if shotgun_bp:
    shotgun_cdo = unreal.get_default_object(shotgun_bp.generated_class())
    
    warp_shake = unreal.load_asset(f"{SHAKE_DIR}/CS_ParryWarp")
    splash_class = unreal.load_asset(f"{VFX_DIR}/WBP_BloodSplash")
    
    if warp_shake:
        shotgun_cdo.set_editor_property("parry_warp_camera_shake", warp_shake.generated_class())
        shotgun_cdo.set_editor_property("parry_warp_shake_scale", 2.0)
        print(f"  Shotgun.ParryWarpCameraShake = CS_ParryWarp (Scale=2.0)")
    if splash_class:
        shotgun_cdo.set_editor_property("parry_blood_splash_widget_class", splash_class.generated_class())
        shotgun_cdo.set_editor_property("parry_blood_splash_duration", 0.4)
        print(f"  Shotgun.ParryBloodSplashWidgetClass = WBP_BloodSplash (Duration=0.4)")
    
    shotgun_cdo.modify()
    EAL.save_loaded_asset(shotgun_bp)
    print(f"  Shotgun BP 저장 완료!")
else:
    print("  Shotgun BP 로드 실패!")

print("\n" + "=" * 60)
print("  완료! 나이아가라 VFX는 에디터에서 수동 생성 필요")
print("  (기존 NS_Hit_Explosion 등을 Pistol/Shotgun에 세팅)")
print("=" * 60)
