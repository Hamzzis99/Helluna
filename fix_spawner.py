import unreal

asset_path = '/Game/Enemy/Melee/BP_MeleeMassSpawner'
backup_path = '/Game/Enemy/Melee/BP_MeleeMassSpawner_OLD'
config_path = '/Game/Enemy/Melee/DA_EnemyMassConfig_Melee'

lines = []

# Step 1: Restore backup if exists (from previous failed run)
if not unreal.EditorAssetLibrary.does_asset_exist(asset_path) and unreal.EditorAssetLibrary.does_asset_exist(backup_path):
    unreal.EditorAssetLibrary.rename_asset(backup_path, asset_path)
    lines.append('Restored backup')

# Step 2: Duplicate the Range spawner as a base (it has the same parent class and works)
range_path = '/Game/Enemy/Ranged/BP_RangeMassSpawner'
temp_path = '/Game/Enemy/Melee/BP_MeleeMassSpawner_NEW'

if unreal.EditorAssetLibrary.does_asset_exist(range_path):
    # Duplicate range spawner
    if unreal.EditorAssetLibrary.does_asset_exist(temp_path):
        unreal.EditorAssetLibrary.delete_asset(temp_path)

    result = unreal.EditorAssetLibrary.duplicate_asset(range_path, temp_path)
    lines.append(f'Duplicated range spawner: {result}')

    if result:
        new_bp = unreal.load_asset(temp_path)
        if new_bp:
            gen_class = new_bp.generated_class()
            cdo = unreal.get_default_object(gen_class)

            # Update entity_types to melee config
            config_asset = unreal.load_asset(config_path)
            if config_asset:
                entry = unreal.MassSpawnedEntityType()
                entry.set_editor_property('entity_config', config_asset)
                entry.set_editor_property('proportion', 1.0)
                cdo.set_editor_property('entity_types', [entry])
                lines.append('Set melee entity config')

            unreal.BlueprintEditorLibrary.compile_blueprint(new_bp)
            unreal.EditorAssetLibrary.save_asset(temp_path)
            lines.append('Compiled and saved new BP')

            # Now swap: delete old, rename new
            if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
                unreal.EditorAssetLibrary.delete_asset(asset_path)
                lines.append('Deleted old BP')

            unreal.EditorAssetLibrary.rename_asset(temp_path, asset_path)
            lines.append('Renamed new BP to original path')

            # Verify
            verify_bp = unreal.load_asset(asset_path)
            if verify_bp:
                verify_cdo = unreal.get_default_object(verify_bp.generated_class())
                et = verify_cdo.get_editor_property('entity_types')
                lines.append(f'Verification: entity_types count = {len(et)}')
                lines.append('SUCCESS')
            else:
                lines.append('ERROR: verification failed')
        else:
            lines.append('ERROR: Could not load duplicated BP')
    else:
        lines.append('ERROR: Duplication failed')
else:
    lines.append('ERROR: Range spawner not found')

with open('E:/Build/bp_fix_result.txt', 'w', encoding='utf-8') as f:
    f.write('\n'.join(lines))
