"""
ARACHNE project setup - run inside the Unreal Editor:
    Tools > Execute Python Script...  (or console:  py "<project>/Scripts/setup_arachne.py")

1. imports Arachne_Skeletal.fbx as /Game/ARACHNE/Characters/SK_Arachne
2. creates simple chitin materials and assigns them by slot name
3. creates BP_Arachne / BP_GM_Arachne (children of the C++ classes, for tweaking in the editor)
4. builds the test map /Game/ARACHNE/Levels/L_ArachneGym (floor, walls, ceiling, boxes, ramp, column,
   tunnel, stairs, moving + rotating platforms)
Safe to re-run: assets are replaced, the gym map is rebuilt.
"""
import unreal, os, json

ROOT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
FBX = r'C:\Users\ptahs\Desktop\ART\Personal\ARACHNE\ArachneVol1\model\Arachne_Skeletal.fbx'
BASE = '/Game/ARACHNE'
CHAR, MATS, BPS, LEVELS = BASE + '/Characters', BASE + '/Materials', BASE + '/Blueprints', BASE + '/Levels'
MESH_PATH = CHAR + '/SK_Arachne'
MAP_PATH = LEVELS + '/L_ArachneGym'

tools = unreal.AssetToolsHelpers.get_asset_tools()
assets = unreal.EditorAssetLibrary
for folder in (CHAR, MATS, BPS, LEVELS):
    assets.make_directory(folder)

# ------------------------------------------------------------------------------------------------ import
def import_mesh():
    # Prefer the legacy FBX path (honours FbxImportUI); harmless if the cvar does not exist.
    unreal.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')
    task = unreal.AssetImportTask()
    task.filename = FBX
    task.destination_path = CHAR
    task.destination_name = 'SK_Arachne'
    task.automated = True
    task.replace_existing = True
    task.save = True
    opt = unreal.FbxImportUI()
    opt.automated_import_should_detect_type = False
    opt.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    opt.import_as_skeletal = True
    opt.import_mesh = True
    opt.import_animations = False
    opt.import_materials = False
    opt.import_textures = False
    opt.create_physics_asset = False
    data = opt.skeletal_mesh_import_data
    data.import_uniform_scale = 1.0
    data.convert_scene = True
    data.convert_scene_unit = True
    data.force_front_x_axis = False
    data.normal_import_method = unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS
    task.options = opt
    tools.import_asset_tasks([task])
    imported = list(task.imported_object_paths)
    unreal.log('ARACHNE import produced: %s' % imported)

    mesh = unreal.load_asset(MESH_PATH)
    if not isinstance(mesh, unreal.SkeletalMesh):
        # Interchange may name the asset after the FBX node - find it and move it into place.
        for path in imported:
            obj = unreal.load_asset(path.split('.')[0])
            if isinstance(obj, unreal.SkeletalMesh):
                if obj.get_path_name().split('.')[0] != MESH_PATH:
                    assets.rename_asset(obj.get_path_name().split('.')[0], MESH_PATH)
                mesh = unreal.load_asset(MESH_PATH)
                break
    assert isinstance(mesh, unreal.SkeletalMesh), 'SK_Arachne not imported: %s' % imported
    return mesh, imported

mesh, imported = import_mesh()

# ------------------------------------------------------------------------------------------------ materials
lib = unreal.MaterialEditingLibrary
def material(name, color, rough=.6, metal=0.0, spec=.5):
    path = MATS + '/' + name
    m = unreal.load_asset(path)
    if not m:
        m = tools.create_asset(name, MATS, unreal.Material, unreal.MaterialFactoryNew())
    lib.delete_all_material_expressions(m)
    col = lib.create_material_expression(m, unreal.MaterialExpressionConstant3Vector, -300, 0)
    col.set_editor_property('constant', unreal.LinearColor(color[0], color[1], color[2], 1))
    lib.connect_material_property(col, '', unreal.MaterialProperty.MP_BASE_COLOR)
    for y, value, prop in ((140, rough, unreal.MaterialProperty.MP_ROUGHNESS),
                           (260, metal, unreal.MaterialProperty.MP_METALLIC),
                           (380, spec, unreal.MaterialProperty.MP_SPECULAR)):
        v = lib.create_material_expression(m, unreal.MaterialExpressionConstant, -300, y)
        v.set_editor_property('r', value)
        lib.connect_material_property(v, '', prop)
    lib.recompile_material(m)
    assets.save_loaded_asset(m)
    return m

chitin  = material('M_ArachneChitin',  (.09, .04, .021), .52)
abdomen = material('M_ArachneAbdomen', (.045, .026, .018), .68)
edge    = material('M_ArachneEdges',   (.23, .10, .036), .5)
eye     = material('M_ArachneEyes',    (.004, .006, .008), .06, 0, .9)
fang    = material('M_ArachneFangs',   (.015, .011, .009), .22)
joint   = material('M_ArachneJoints',  (.025, .015, .012), .7)

slots = mesh.get_editor_property('materials')
for slot in slots:
    name = str(slot.get_editor_property('material_slot_name')).lower() + ' ' + \
           str(slot.get_editor_property('imported_material_slot_name')).lower()
    m = eye if 'eye' in name else fang if 'fang' in name else abdomen if 'abdomen' in name else \
        joint if 'joint' in name else edge if ('ochre' in name or 'edge' in name) else chitin
    slot.set_editor_property('material_interface', m)
mesh.set_editor_property('materials', slots)
assets.save_loaded_asset(mesh)

# ------------------------------------------------------------------------------------------------ blueprints
def blueprint(name, parent_path):
    path = BPS + '/' + name
    bp = unreal.load_asset(path)
    if not bp:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property('parent_class', unreal.load_class(None, parent_path))
        bp = tools.create_asset(name, BPS, unreal.Blueprint, factory)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assets.save_loaded_asset(bp)
    return bp

pawn_bp = blueprint('BP_Arachne', '/Script/Arachne.ArachnePawn')
pawn_cdo = unreal.get_default_object(unreal.BlueprintEditorLibrary.generated_class(pawn_bp))
pawn_cdo.set_editor_property('spider_asset', mesh)
gm_bp = blueprint('BP_GM_Arachne', '/Script/Arachne.GM_ArachneBase')
gm_class = unreal.BlueprintEditorLibrary.generated_class(gm_bp)
unreal.get_default_object(gm_class).set_editor_property('default_pawn_class', unreal.BlueprintEditorLibrary.generated_class(pawn_bp))
assets.save_loaded_asset(pawn_bp)
assets.save_loaded_asset(gm_bp)

# ------------------------------------------------------------------------------------------------ gym map
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if assets.does_asset_exist(MAP_PATH):
    unreal.EditorLoadingAndSavingUtils.new_blank_map(False)   # release the old map before replacing it
    assets.delete_asset(MAP_PATH)
assert level.new_level(MAP_PATH), 'could not create ' + MAP_PATH

floor_mat = material('M_GymFloor', (.11, .15, .18), .85)
wall_mat = material('M_GymWall', (.20, .27, .29), .85)
obstacle_mat = material('M_GymObstacle', (.42, .15, .045), .72)
grid_mat = material('M_GymGrid', (.23, .32, .35), .8)
mover_mat = material('M_GymMover', (.05, .22, .30), .5)
cube = unreal.load_asset('/Engine/BasicShapes/Cube')
cylinder = unreal.load_asset('/Engine/BasicShapes/Cylinder')

def box(label, loc, scale, mat=wall_mat, rot=None, collide=True, shape=None):
    a = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*loc), rot or unreal.Rotator())
    a.set_actor_label(label)
    c = a.static_mesh_component
    c.set_static_mesh(shape or cube)
    a.set_actor_scale3d(unreal.Vector(*scale))
    c.set_material(0, mat)
    c.set_collision_profile_name('BlockAll' if collide else 'NoCollision')
    return a

# Cube = 100 cm, so scale 1 = 1 m. Floor top at z = 0.
box('Floor', (0, 0, -25), (40, 34, .5), floor_mat)
box('Climbing wall', (1650, 0, 400), (.5, 34, 8.5))
box('Ceiling', (800, 0, 825), (17, 34, .5))
box('Left wall', (0, -1700, 400), (40, .5, 8.5))
box('Right wall', (0, 1700, 400), (40, .5, 8.5))
box('Back wall', (-2000, 0, 400), (.5, 34, 8.5))
box('Low step 30cm', (300, -500, 15), (2.5, 3, .3), obstacle_mat)
box('Climb-over block 180cm', (750, -500, 90), (3, 4, 1.8), obstacle_mat)
box('Tall pillar box', (1150, -1150, 250), (2, 2, 5), obstacle_mat)
box('Ramp 25deg', (250, 600, 165), (7, 4, .5), obstacle_mat, unreal.Rotator(pitch=25))
box('Steep slab 65deg', (900, 750, 250), (5, 3, .5), obstacle_mat, unreal.Rotator(pitch=65))
box('Round column', (-650, -650, 300), (2.4, 2.4, 6), obstacle_mat, shape=cylinder)
# tunnel: crawl in, stick to the low ceiling
box('Tunnel roof', (-900, 900, 185), (6, 3.5, .3), wall_mat)
box('Tunnel side A', (-900, 710, 85), (6, .2, 1.7), wall_mat)
box('Tunnel side B', (-900, 1090, 85), (6, .2, 1.7), wall_mat)
# stairs
for i in range(6):
    box('Stair %d' % i, (-1400 + i * 60, -1200, 12.5 + i * 25), (.6, 3, .25 + i * .5), obstacle_mat)
for k in range(-9, 10):
    box('Grid X %d' % k, (k * 200, 0, .15), (.012, 34, .002), grid_mat, collide=False)
    box('Grid Y %d' % k, (0, k * 200, .15), (40, .012, .002), grid_mat, collide=False)

mover_class = unreal.load_class(None, '/Script/Arachne.ArachneMover')
def mover(label, loc, scale, amplitude, period, spin=None):
    a = actors.spawn_actor_from_class(mover_class, unreal.Vector(*loc))
    a.set_actor_label(label)
    a.set_actor_scale3d(unreal.Vector(*scale))
    a.set_editor_property('amplitude', unreal.Vector(*amplitude))
    a.set_editor_property('period', period)
    if spin:
        a.set_editor_property('spin_rate', unreal.Rotator(**spin))
    a.get_editor_property('mesh').set_material(0, mover_mat)
    return a
mover('Moving platform', (-1100, -300, 40), (3, 3, .3), (0, 0, 120), 5.0)
mover('Rotating platform', (-300, 1250, 20), (4, 4, .3), (0, 0, 0), 1.0, {'yaw': 25})

start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-1300, 0, 120))
start.set_actor_label('Arachne Player Start')
sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(-500, -500, 1200), unreal.Rotator(pitch=-50, yaw=-35))
sun.light_component.set_editor_property('intensity', 4.0)
sun.light_component.set_editor_property('atmosphere_sun_light', True)
actors.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0))
sky = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 1000))
sky.light_component.set_editor_property('real_time_capture', True)
sky.light_component.set_editor_property('intensity', 1.0)
for x in (-1200, 0, 1200):
    for y in (-1000, 1000):
        light = actors.spawn_actor_from_class(unreal.PointLight, unreal.Vector(x, y, 650))
        c = light.point_light_component
        c.set_editor_property('intensity', 15000.0)
        c.set_editor_property('attenuation_radius', 1600.0)
        c.set_editor_property('source_radius', 40.0)

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
world.get_world_settings().set_editor_property('default_game_mode', gm_class)
level.save_current_level()
assets.save_directory(BASE, only_if_is_dirty=True, recursive=True)

report = {
    'skeletal_mesh': mesh.get_path_name(),
    'skeleton': mesh.get_editor_property('skeleton').get_path_name(),
    'pawn': pawn_bp.get_path_name(),
    'game_mode': gm_bp.get_path_name(),
    'map': MAP_PATH,
    'material_slots': len(slots),
    'imported': imported,
}
with open(os.path.join(ROOT, 'Saved', 'ArachneImport.json'), 'w') as f:
    json.dump(report, f, indent=2)
unreal.log('ARACHNE_IMPORT_COMPLETE ' + json.dumps(report))
