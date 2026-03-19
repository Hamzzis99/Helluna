"""
NS_ParryWarpTrail 나이아가라 시스템 생성 스크립트
=================================================
패링 워프 시 빛 잔상 이펙트용 나이아가라 시스템을 생성하고
User Parameter(WarpColor)를 추가합니다.

실행 방법:
  에디터 Output Log(Python):
  py "D:/UnrealProject/Capston_Project/MyHelluna/Scripts/create_parry_warp_trail.py"

생성 후 수동 작업 필요:
  1. Emitter 3개 구성 (E_LightBurst, E_RibbonTrail, E_GlitchFlash)
  2. 각 Emitter 모듈 설정 (Spawn Burst, Scale Sprite Size 등)
  3. Step 3 머티리얼 연결
"""
import unreal

# ============================================================
# 설정
# ============================================================
ASSET_PATH = '/Game/Gihyeon/Combat/VFX'
ASSET_NAME = 'NS_ParryWarpTrail'
FULL_PATH = f'{ASSET_PATH}/{ASSET_NAME}'

# ============================================================
# 1. 폴더 생성
# ============================================================
if not unreal.EditorAssetLibrary.does_directory_exist(ASSET_PATH):
    unreal.EditorAssetLibrary.make_directory(ASSET_PATH)
    unreal.log(f'[ParryWarpTrail] 폴더 생성: {ASSET_PATH}')
else:
    unreal.log(f'[ParryWarpTrail] 폴더 이미 존재: {ASSET_PATH}')

# ============================================================
# 2. 기존 에셋 체크
# ============================================================
if unreal.EditorAssetLibrary.does_asset_exist(FULL_PATH):
    unreal.log_warning(f'[ParryWarpTrail] 에셋이 이미 존재합니다: {FULL_PATH}')
    unreal.log_warning('[ParryWarpTrail] 덮어쓰려면 먼저 삭제 후 다시 실행하세요.')
else:
    # ============================================================
    # 3. NiagaraSystem 에셋 생성
    # ============================================================
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    factory = unreal.NiagaraSystemFactoryNew()

    niagara_system = asset_tools.create_asset(
        asset_name=ASSET_NAME,
        package_path=ASSET_PATH,
        asset_class=unreal.NiagaraSystem,
        factory=factory
    )

    if niagara_system is not None:
        unreal.log(f'[ParryWarpTrail] 나이아가라 시스템 생성 완료: {FULL_PATH}')

        # 저장
        unreal.EditorAssetLibrary.save_asset(FULL_PATH, only_if_is_dirty=False)
        unreal.log(f'[ParryWarpTrail] 에셋 저장 완료')
    else:
        unreal.log_error('[ParryWarpTrail] 나이아가라 시스템 생성 실패!')

# ============================================================
# 4. 수동 작업 안내
# ============================================================
unreal.log('=' * 60)
unreal.log('[ParryWarpTrail] === 수동 작업 안내 ===')
unreal.log('=' * 60)
unreal.log(f'[ParryWarpTrail] 1. Content Browser에서 {FULL_PATH} 더블클릭')
unreal.log('[ParryWarpTrail] 2. User Parameter 추가: WarpColor (Linear Color, 기본 0.2/0.5/1.0/1.0)')
unreal.log('[ParryWarpTrail] 3. Emitter 1 (E_LightBurst):')
unreal.log('[ParryWarpTrail]    - Fountain 템플릿 기반 또는 Empty → Spawn Burst=30')
unreal.log('[ParryWarpTrail]    - Lifetime=0.3~0.6, SpriteSize=10~30')
unreal.log('[ParryWarpTrail]    - Shape Location Sphere R=50, Add Velocity 랜덤')
unreal.log('[ParryWarpTrail]    - Scale Sprite Size 커브: 1→0, Color=WarpColor 링크')
unreal.log('[ParryWarpTrail] 4. Emitter 2 (E_RibbonTrail):')
unreal.log('[ParryWarpTrail]    - Empty → Spawn Burst=8, Lifetime=0.2~0.4')
unreal.log('[ParryWarpTrail]    - Ribbon Renderer, Width=5, Color=WarpColor')
unreal.log('[ParryWarpTrail] 5. Emitter 3 (E_GlitchFlash):')
unreal.log('[ParryWarpTrail]    - Empty → Spawn Burst=1, Lifetime=0.1')
unreal.log('[ParryWarpTrail]    - SpriteSize=200, Color=White→WarpColor→투명')
unreal.log('[ParryWarpTrail] 6. 저장 후 권총 BP의 ParryWarpEffect에 할당')
unreal.log('=' * 60)
