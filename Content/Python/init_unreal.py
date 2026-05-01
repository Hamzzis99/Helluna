"""
Helluna 프로젝트 에디터 시작 스크립트
- 뷰포트 Realtime 자동 활성화 (Landscape Grass 표시에 필수)
- Grass 관련 콘솔 변수 설정

주의: editor_set_viewport_realtime은 UE 5.7.2에서 Override 시스템을 씀.
콜백이 반복 발동되면 RemoveRealtimeOverride ensure 실패 → MCP 브리지 끊김 가능.
→ 1회 가드 + 성공/실패 무관 즉시 언레지스터.
"""
import unreal

_handle = None
_done = False  # 중복 실행 방지 가드


def _enable_realtime_viewport(delta_time):
    """에디터 로드 완료 후 Realtime 뷰포트 활성화 (1회만 실행)"""
    global _handle, _done

    # 언레지스터가 지연되어 콜백이 재호출되더라도 본문 1회만 실행
    if _done:
        return
    _done = True

    # 먼저 콜백을 해제해 후속 틱에서 절대 재진입 안 되게 함
    if _handle is not None:
        try:
            unreal.unregister_slate_post_tick_callback(_handle)
        except Exception:
            pass
        _handle = None

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


# 에디터가 완전히 로드된 뒤 실행되도록 틱 지연
_handle = unreal.register_slate_post_tick_callback(_enable_realtime_viewport)
unreal.log("Helluna: init_unreal.py loaded — Realtime viewport will activate after editor init")
