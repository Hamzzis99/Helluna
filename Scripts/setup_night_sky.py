"""
Night Sky Setup Script for Ultra Dynamic Sky
Reference: Full moon, snowy mountains, deep blue tones, clear sky, slight fog
Run this in Unreal Editor: Tools > Execute Python Script
Or in Output Log (Python): py "D:/UnrealProject/Capston_Project/Helluna/Scripts/setup_night_sky.py"
"""
import unreal

# ============================================================
# 1. Load UDS Blueprint Class
# ============================================================
uds_bp_path = '/Game/UltraDynamicSky/Blueprints/Ultra_Dynamic_Sky.Ultra_Dynamic_Sky_C'
uds_class = unreal.load_class(None, uds_bp_path)

if not uds_class:
    # Try alternative loading
    uds_asset = unreal.EditorAssetLibrary.load_asset('/Game/UltraDynamicSky/Blueprints/Ultra_Dynamic_Sky')
    if uds_asset:
        uds_class = unreal.load_class(None, uds_bp_path)
    
if not uds_class:
    unreal.log_error("Could not load Ultra_Dynamic_Sky class! Make sure UDS plugin is installed.")
else:
    unreal.log("Ultra_Dynamic_Sky class loaded successfully!")

# ============================================================
# 2. Check if UDS already exists in level, if not spawn it
# ============================================================
editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor_subsystem.get_editor_world()

# Find existing UDS actors
existing_uds = unreal.GameplayStatics.get_all_actors_of_class(world, uds_class)

if len(existing_uds) > 0:
    uds_actor = existing_uds[0]
    unreal.log(f"Found existing UDS actor: {uds_actor.get_name()}")
else:
    # Spawn new UDS actor
    spawn_location = unreal.Vector(0, 0, 0)
    spawn_rotation = unreal.Rotator(0, 0, 0)
    uds_actor = unreal.EditorLevelLibrary.spawn_actor_from_class(uds_class, spawn_location, spawn_rotation)
    unreal.log(f"Spawned new UDS actor: {uds_actor.get_name()}")

# ============================================================
# 3. Configure Night Sky Parameters (matching reference image)
# ============================================================
# Reference: Deep blue moonlit night, full moon, clear sky, snowy mountains

# --- TIME ---
# 23.5 = 11:30 PM (deep night with moon high)
uds_actor.set_editor_property('Time of Day', 23.5)

# --- SKY MODE ---
# Cloud Coverage: very low for clear night sky
uds_actor.set_editor_property('Cloud Coverage', 0.05)

# --- MOON ---
# Full moon (phase 0.5 = full), large scale for dramatic effect
uds_actor.set_editor_property('Moon Scale', 2.5)
uds_actor.set_editor_property('Moon Phase', 0.5)

# Moon position - high in the sky, slightly right
uds_actor.set_editor_property('Moon Pitch', 35.0)
uds_actor.set_editor_property('Moon Yaw', 45.0)

# --- STARS ---
uds_actor.set_editor_property('Stars Intensity', 1.5)

# --- NIGHT BRIGHTNESS ---
# Higher for the blue moonlit glow
uds_actor.set_editor_property('Night Brightness', 3.0)

# --- FOG ---
# Slight fog for atmospheric depth
uds_actor.set_editor_property('Fog', 0.15)

# --- OVERALL INTENSITY ---
uds_actor.set_editor_property('Overall Intensity', 1.2)

# --- AURORA (subtle for mystical touch) ---
uds_actor.set_editor_property('Aurora Intensity', 0.0)

unreal.log("============================================")
unreal.log("Night Sky configuration applied successfully!")
unreal.log("Time of Day: 23.5 (11:30 PM)")
unreal.log("Cloud Coverage: 0.05 (nearly clear)")
unreal.log("Moon: Full (Phase 0.5), Scale 2.5x")
unreal.log("Stars: 1.5x intensity")
unreal.log("Night Brightness: 3.0")
unreal.log("Fog: 0.15 (subtle)")
unreal.log("============================================")
