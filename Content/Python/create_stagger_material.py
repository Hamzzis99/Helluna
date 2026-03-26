import unreal

EAL = unreal.EditorAssetLibrary
AF = unreal.AssetToolsHelpers.get_asset_tools()

mat_path = "/Game/Gihyeon/Combat/Parry/M_StaggerOverlay"
if EAL.does_asset_exist(mat_path):
    print("M_StaggerOverlay already exists!")
else:
    factory = unreal.MaterialFactoryNew()
    mat = AF.create_asset("M_StaggerOverlay", "/Game/Gihyeon/Combat/Parry", unreal.Material, factory)
    if mat:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        mat.modify()
        EAL.save_loaded_asset(mat)
        print("M_StaggerOverlay created! (Translucent + Unlit)")
    else:
        print("FAILED!")

parry_ga_path = "/Game/Hero/HeroGameplayAbility/BP_HeroGameplayAbility_GunParry"
if not EAL.does_asset_exist(parry_ga_path):
    parry_ga_path = "/Game/Gihyeon/Combat/Parry/BP_HeroGameplayAbility_GunParry"

mat_asset = unreal.load_asset(mat_path)
if mat_asset:
    ga_bp = unreal.load_asset(parry_ga_path)
    if ga_bp:
        cdo = unreal.get_default_object(ga_bp.generated_class())
        cdo.set_editor_property("stagger_overlay_material", mat_asset)
        cdo.modify()
        EAL.save_loaded_asset(ga_bp)
        print(f"GunParry BP: StaggerOverlayMaterial = M_StaggerOverlay")
    else:
        print(f"GunParry BP not found at {parry_ga_path}")
        print("Search for it:")
        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        all_a = registry.get_all_assets()
        for a in all_a:
            if "GunParry" in str(a.asset_name) or "Parry" in str(a.asset_name):
                cls = str(a.asset_class_path.asset_name)
                if cls == "Blueprint":
                    print(f"  {a.package_path}/{a.asset_name}")

print("Done! Now open M_StaggerOverlay in editor and:")
print("  1. Add Constant3Vector node -> (1.0, 0.5, 0.0) orange -> connect to Emissive Color")
print("  2. Add Scalar Constant -> 0.3 -> connect to Opacity")
print("  3. Save!")
