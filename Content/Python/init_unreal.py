# File: D:/UnrealProject/Capston_Project/Helluna/Content/Python/init_unreal.py
# Target: HellunaEditor, UE 5.8.1 (editor Python; no C++ build required)
"""
Helluna 프로젝트 에디터 시작 스크립트
- 사용자가 저장한 뷰포트 Realtime 설정 유지
- Grass 관련 콘솔 변수 설정

UE 5.8.1의 editor_set_viewport_realtime(True)는 기존 override를 제거한다.
이 스크립트는 override를 등록하지 않으므로 해당 API를 호출하지 않는다.
"""
import unreal

_handle = None
_done = False  # 중복 실행 방지 가드


def _apply_startup_settings(delta_time):
    """에디터 로드 후 기존 Grass 설정을 1회만 적용한다."""
    global _handle, _done

    # 언레지스터가 지연되어 콜백이 재호출되더라도 본문 1회만 실행
    if _done:
        return
    _done = True

    # 먼저 콜백을 해제해 후속 틱에서 절대 재진입 안 되게 함
    if _handle is not None:
        try:
            unreal.unregister_slate_post_tick_callback(_handle)
        except Exception as e:
            unreal.log_warning(f"Helluna: Failed to unregister startup callback: {e}")
        _handle = None

    try:
        editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        if editor is None:
            unreal.log_warning("Helluna: Startup settings skipped; editor subsystem unavailable")
            return
        world = editor.get_editor_world()
        if world is None:
            unreal.log_warning("Helluna: Startup settings skipped; editor world unavailable")
            return
        unreal.SystemLibrary.execute_console_command(world, "grass.Enable 1")
        unreal.SystemLibrary.execute_console_command(world, "foliage.DensityScale 1.0")
        unreal.SystemLibrary.execute_console_command(world, "sg.FoliageQuality 3")
        unreal.log("Helluna: Startup grass settings applied; viewport Realtime left unchanged")
    except Exception as e:
        unreal.log_warning(f"Helluna: Failed to apply startup grass settings: {e}")


# 에디터가 완전히 로드된 뒤 실행되도록 틱 지연
_handle = unreal.register_slate_post_tick_callback(_apply_startup_settings)
unreal.log("Helluna: init_unreal.py loaded; grass settings scheduled after editor init")
