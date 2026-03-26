import unreal

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
if world is None:
    unreal.log_warning("[AimDiag] PIE world not found - PIE 실행 중이어야 합니다")
else:
    all_chars = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Character)
    unreal.log_warning(f"[AimDiag] Total Characters: {len(all_chars)}")
    for c in all_chars:
        name = c.get_name()
        cls = c.get_class().get_name()
        if "Hero" not in cls:
            continue
        unreal.log_warning(f"[AimDiag] Found: {name} ({cls})")
        
        asc = c.get_ability_system_component() if hasattr(c, 'get_ability_system_component') else None
        if asc is None:
            try:
                asc = c.get_helluna_ability_system_component()
            except:
                pass
        
        if asc:
            aim_tag = unreal.GameplayTag.request_gameplay_tag("Player.status.Aim")
            has_aim = asc.has_matching_gameplay_tag(aim_tag)
            unreal.log_warning(f"[AimDiag]   Player.status.Aim = {has_aim}")
            
            all_tags = asc.get_owned_gameplay_tags()
            unreal.log_warning(f"[AimDiag]   All tags: {all_tags}")
        else:
            unreal.log_warning(f"[AimDiag]   ASC not accessible via Python")
        
        weapon = None
        try:
            weapon = c.get_current_weapon()
        except:
            pass
        unreal.log_warning(f"[AimDiag]   CurrentWeapon: {weapon.get_name() if weapon else 'None'}")
        
        cam = None
        try:
            cam = c.get_follow_camera()
        except:
            pass
        if cam:
            unreal.log_warning(f"[AimDiag]   FOV: {cam.field_of_view}")
        
        boom = None
        try:
            boom = c.get_camera_boom()
        except:
            pass
        if boom:
            unreal.log_warning(f"[AimDiag]   ArmLength: {boom.target_arm_length}")
