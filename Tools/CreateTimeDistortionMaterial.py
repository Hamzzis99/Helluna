"""
Create M_PP_TimeDistortion post-process material.

UE 5.7 — run via:
  UnrealEditor-Cmd.exe Helluna.uproject -run=pythonscript -script="Tools/CreateTimeDistortionMaterial.py"

Result: /Game/BossEvent/Materials/M_PP_TimeDistortion
"""
import unreal

PACKAGE_PATH = "/Game/BossEvent/Materials"
ASSET_NAME = "M_PP_TimeDistortion"
FULL_PATH = f"{PACKAGE_PATH}/{ASSET_NAME}"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary

if unreal.EditorAssetLibrary.does_asset_exist(FULL_PATH):
    unreal.EditorAssetLibrary.delete_asset(FULL_PATH)

mat = asset_tools.create_asset(
    asset_name=ASSET_NAME,
    package_path=PACKAGE_PATH,
    asset_class=unreal.Material,
    factory=unreal.MaterialFactoryNew(),
)

mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
# BL_SCENE_COLOR_AFTER_TONEMAPPING in UE 5.5+
try:
    mat.set_editor_property("blendable_location",
                            unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
except Exception:
    mat.set_editor_property("blendable_location", 0)

def expr(cls, x, y):
    return mel.create_material_expression(mat, cls, x, y)

# --- Parameters ---
p_center = expr(unreal.MaterialExpressionVectorParameter, -1800, 200)
p_center.set_editor_property("parameter_name", "ZoneCenter")
p_center.set_editor_property("default_value", unreal.LinearColor(0, 0, 0, 0))

p_radius = expr(unreal.MaterialExpressionScalarParameter, -1800, 350)
p_radius.set_editor_property("parameter_name", "ZoneRadius")
p_radius.set_editor_property("default_value", 1000.0)

p_strength = expr(unreal.MaterialExpressionScalarParameter, -1800, 450)
p_strength.set_editor_property("parameter_name", "Strength")
p_strength.set_editor_property("default_value", 1.0)

p_edge = expr(unreal.MaterialExpressionScalarParameter, -1800, 550)
p_edge.set_editor_property("parameter_name", "EdgeSoftness")
p_edge.set_editor_property("default_value", 150.0)

# --- Scene color ---
scene = expr(unreal.MaterialExpressionSceneTexture, -1800, -400)
scene.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)

# --- Gray via Desaturation (fraction=1 — full grayscale) ---
gray = expr(unreal.MaterialExpressionDesaturation, -1200, -300)
gray_frac = expr(unreal.MaterialExpressionConstant, -1400, -200)
gray_frac.set_editor_property("r", 1.0)
mel.connect_material_expressions(scene, "Color", gray, "")
mel.connect_material_expressions(gray_frac, "", gray, "Fraction")

# --- World position (PP material reconstructs from depth) ---
wpos = expr(unreal.MaterialExpressionWorldPosition, -1800, 100)

# --- Distance to ZoneCenter ---
dist = expr(unreal.MaterialExpressionDistance, -1400, 200)
mel.connect_material_expressions(wpos, "", dist, "A")
mel.connect_material_expressions(p_center, "", dist, "B")

# --- Signed = Radius - Dist ---
signed = expr(unreal.MaterialExpressionSubtract, -1100, 300)
mel.connect_material_expressions(p_radius, "", signed, "A")
mel.connect_material_expressions(dist, "", signed, "B")

# --- /EdgeSoftness ---
divided = expr(unreal.MaterialExpressionDivide, -900, 350)
mel.connect_material_expressions(signed, "", divided, "A")
mel.connect_material_expressions(p_edge, "", divided, "B")

# --- Saturate -> mask 0..1 ---
sat = expr(unreal.MaterialExpressionSaturate, -700, 400)
mel.connect_material_expressions(divided, "", sat, "")

# --- Stencil exclusion (custom depth = 1 means excluded) ---
stencil = expr(unreal.MaterialExpressionSceneTexture, -1800, 700)
stencil.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_CUSTOM_STENCIL)

stencil_r = expr(unreal.MaterialExpressionComponentMask, -1500, 750)
stencil_r.set_editor_property("r", True)
stencil_r.set_editor_property("g", False)
stencil_r.set_editor_property("b", False)
stencil_r.set_editor_property("a", False)
mel.connect_material_expressions(stencil, "Color", stencil_r, "")

c_zero = expr(unreal.MaterialExpressionConstant, -1300, 900)
c_zero.set_editor_property("r", 0.0)
c_one = expr(unreal.MaterialExpressionConstant, -1300, 970)
c_one.set_editor_property("r", 1.0)
c_half = expr(unreal.MaterialExpressionConstant, -1300, 1040)
c_half.set_editor_property("r", 0.5)

if_stencil = expr(unreal.MaterialExpressionIf, -1000, 800)
mel.connect_material_expressions(stencil_r, "", if_stencil, "A")
mel.connect_material_expressions(c_half, "", if_stencil, "B")
mel.connect_material_expressions(c_zero, "", if_stencil, "A > B")
mel.connect_material_expressions(c_one,  "", if_stencil, "A == B")
mel.connect_material_expressions(c_one,  "", if_stencil, "A < B")

# --- Final mask = Saturate(...) * StencilGate * Strength ---
mul1 = expr(unreal.MaterialExpressionMultiply, -500, 500)
mel.connect_material_expressions(sat, "", mul1, "A")
mel.connect_material_expressions(if_stencil, "", mul1, "B")

mul2 = expr(unreal.MaterialExpressionMultiply, -300, 550)
mel.connect_material_expressions(mul1, "", mul2, "A")
mel.connect_material_expressions(p_strength, "", mul2, "B")

# --- Lerp(Original, Gray, mask) ---
lerp = expr(unreal.MaterialExpressionLinearInterpolate, 100, 0)
mel.connect_material_expressions(scene, "Color", lerp, "A")
mel.connect_material_expressions(gray, "", lerp, "B")
mel.connect_material_expressions(mul2, "", lerp, "Alpha")

mel.connect_material_property(lerp, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

mel.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(FULL_PATH)
unreal.log(f"[CreateTimeDistortionMaterial] OK -> {FULL_PATH}")
