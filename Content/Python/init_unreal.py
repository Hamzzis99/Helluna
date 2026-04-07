"""
Helluna 프로젝트 에디터 시작 스크립트
- 뷰포트 Realtime 자동 활성화 (Landscape Grass 표시에 필수)
- Grass 관련 콘솔 변수 설정
"""
import unreal

_handle = None

def _enable_realtime_viewport(delta_time):
    """에디터 로드 완료 후 Realtime 뷰포트 활성화 (1회 실행 후 해제)"""
    global _handle

    try:
        le = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        if le:
            le.editor_set_viewport_realtime(True)
            unreal.log("Helluna: Viewport Realtime ENABLED (for Landscape Grass)")
    except Exception as e:
        unreal.log_warning(f"Helluna: Failed to enable Realtime viewport: {e}")

    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
        if world:
            unreal.SystemLibrary.execute_console_command(world, "grass.Enable 1")
            unreal.SystemLibrary.execute_console_command(world, "foliage.DensityScale 1.0")
            unreal.SystemLibrary.execute_console_command(world, "sg.FoliageQuality 3")
    except Exception:
        pass

    # 1회 실행 후 콜백 해제
    if _handle is not None:
        unreal.unregister_slate_post_tick_callback(_handle)
        _handle = None

# 에디터가 완전히 로드된 뒤 실행되도록 틱 지연
_handle = unreal.register_slate_post_tick_callback(_enable_realtime_viewport)
unreal.log("Helluna: init_unreal.py loaded — Realtime viewport will activate after editor init")
