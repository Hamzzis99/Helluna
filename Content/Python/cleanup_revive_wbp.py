import unreal
import os

def cleanup_revive_wbp():
    content_dir = r"D:\UnrealProject\Capston_Project\Helluna\Content\UI\Widgets\Downed"
    wbp1 = os.path.join(content_dir, "WBP_HellunaReviveProgress.uasset")
    wbp2 = os.path.join(content_dir, "WBP_HellunaReviveProgress2.uasset")
    
    if not os.path.exists(wbp2):
        unreal.log_warning("[ReviveCleanup] WBP2 not found - already cleaned or renamed")
        return
    
    if os.path.exists(wbp1):
        try:
            os.remove(wbp1)
            unreal.log_warning("[ReviveCleanup] Deleted empty WBP1")
        except Exception as e:
            unreal.log_warning(f"[ReviveCleanup] Cannot delete WBP1: {e}")
            return
    
    try:
        os.rename(wbp2, wbp1)
        unreal.log_warning("[ReviveCleanup] Renamed WBP2 -> WBP_HellunaReviveProgress")
    except Exception as e:
        unreal.log_warning(f"[ReviveCleanup] Rename failed: {e}")
        return

    hero_paths = [
        "/Game/Hero/HeroCharacter/BP_HellunaHero_Lui",
        "/Game/Hero/HeroCharacter/BP_HellunaHero_Liam",
        "/Game/Hero/HeroCharacter/BP_HellunaHero_Luna",
    ]
    
    wbp = unreal.load_asset("/Game/UI/Widgets/Downed/WBP_HellunaReviveProgress")
    if wbp:
        wbp_class = wbp.generated_class()
        for path in hero_paths:
            bp = unreal.load_asset(path)
            if bp:
                bp.modify()
                cdo = unreal.get_default_object(bp.generated_class())
                cdo.modify()
                cdo.set_editor_property("revive_progress_widget_class", wbp_class)
                unreal.EditorAssetLibrary.save_loaded_asset(bp)
                unreal.log_warning(f"[ReviveCleanup] {bp.get_name()} reassigned")
    
    unreal.log_warning("[ReviveCleanup] DONE! Delete this script from Content/Python/")

cleanup_revive_wbp()
