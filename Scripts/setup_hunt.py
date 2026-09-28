"""
ARACHNE hunt setup - run inside the Unreal Editor AFTER compiling the C++ changes:
    Tools > Execute Python Script...  (or console:  py "<project>/Scripts/setup_hunt.py")

1. creates BP_PlayerCharacter (child of ArachnePlayerCharacter) - first-person camera, flashlight, noise/visibility tuning
2. makes sure BP_Arachne exists (child of ArachnePawn) - body, senses, memory, navigator and brain tuning
3. sets BP_GM_Arachne's default pawn to BP_PlayerCharacter
4. builds /Game/ARACHNE/Levels/L_HuntGym: two rooms joined by a corridor (3 m ceilings), patrol waypoints,
   camp points in corners and above a door, Arachne and a player start.
Safe to re-run: blueprints are kept, L_HuntGym is rebuilt. Needs Scripts/setup_arachne.py to have run once (mesh, materials).
"""
import unreal

BASE = '/Game/ARACHNE'
BPS, LEVELS, MATS = BASE + '/Blueprints', BASE + '/Levels', BASE + '/Materials'
MAP_PATH = LEVELS + '/L_HuntGym'

tools = unreal.AssetToolsHelpers.get_asset_tools()
assets = unreal.EditorAssetLibrary
bp_lib = unreal.BlueprintEditorLibrary
for folder in (BPS, LEVELS):
    assets.make_directory(folder)

# ------------------------------------------------------------------------------------------------ blueprints
def blueprint(name, parent_path):
    path = BPS + '/' + name
    bp = unreal.load_asset(path)
    if not bp:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property('parent_class', unreal.load_class(None, parent_path))
        bp = tools.create_asset(name, BPS, unreal.Blueprint, factory)
    bp_lib.compile_blueprint(bp)
    assets.save_loaded_asset(bp)
    return bp

player_bp = blueprint('BP_PlayerCharacter', '/Script/Arachne.ArachnePlayerCharacter')
arachne_bp = blueprint('BP_Arachne', '/Script/Arachne.ArachnePawn')
gm_bp = blueprint('BP_GM_Arachne', '/Script/Arachne.GM_ArachneBase')
player_cls = bp_lib.generated_class(player_bp)
arachne_cls = bp_lib.generated_class(arachne_bp)
gm_cls = bp_lib.generated_class(gm_bp)

arachne_cdo = unreal.get_default_object(arachne_cls)
mesh = unreal.load_asset(BASE + '/Characters/SK_Arachne')
if mesh and not arachne_cdo.get_editor_property('spider_asset'):
    arachne_cdo.set_editor_property('spider_asset', mesh)
unreal.get_default_object(gm_cls).set_editor_property('default_pawn_class', player_cls)
for bp in (player_bp, arachne_bp, gm_bp):
    assets.save_loaded_asset(bp)

# ------------------------------------------------------------------------------------------------ level
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if assets.does_asset_exist(MAP_PATH):
    unreal.EditorLoadingAndSavingUtils.new_blank_map(False)   # release the old map before replacing it
    assets.delete_asset(MAP_PATH)
assert level.new_level(MAP_PATH), 'could not create ' + MAP_PATH

cube = unreal.load_asset('/Engine/BasicShapes/Cube')
floor_mat = unreal.load_asset(MATS + '/M_GymFloor')
wall_mat = unreal.load_asset(MATS + '/M_GymWall')
prop_mat = unreal.load_asset(MATS + '/M_GymObstacle')

def box(label, loc, scale, mat):
    a = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*loc), unreal.Rotator())
    a.set_actor_label(label)
    c = a.static_mesh_component
    c.set_static_mesh(cube)
    a.set_actor_scale3d(unreal.Vector(*scale))
    if mat:
        c.set_material(0, mat)
    c.set_collision_profile_name('BlockAll')
    return a

# Cube = 100 cm, scale 1 = 1 m. Floor top z = 0, ceiling bottom z = 300.
# Room A: x -1000..0 | corridor: x 0..1200, y -150..150 | Room B: x 1200..2200. Rooms span y -600..600.
box('Floor', (600, 0, -10), (32.6, 12.6, .2), floor_mat)
box('Ceiling', (600, 0, 310), (32.6, 12.6, .2), wall_mat)
box('A west wall', (-1010, 0, 150), (.2, 12.4, 3), wall_mat)
box('A north wall', (-500, 610, 150), (10.2, .2, 3), wall_mat)
box('A south wall', (-500, -610, 150), (10.2, .2, 3), wall_mat)
box('A east wall N', (10, 385, 150), (.2, 4.7, 3), wall_mat)
box('A east wall S', (10, -385, 150), (.2, 4.7, 3), wall_mat)
box('Corridor wall N', (600, 160, 150), (11.6, .2, 3), wall_mat)
box('Corridor wall S', (600, -160, 150), (11.6, .2, 3), wall_mat)
box('B west wall N', (1190, 385, 150), (.2, 4.7, 3), wall_mat)
box('B west wall S', (1190, -385, 150), (.2, 4.7, 3), wall_mat)
box('B north wall', (1700, 610, 150), (10.2, .2, 3), wall_mat)
box('B south wall', (1700, -610, 150), (10.2, .2, 3), wall_mat)
box('B east wall', (2210, 0, 150), (.2, 12.4, 3), wall_mat)
box('Table', (-500, 250, 40), (1.6, .9, .8), prop_mat)
box('Wardrobe', (2050, 450, 100), (.9, .5, 2), prop_mat)
box('Crate', (1600, -300, 35), (.7, .7, .7), prop_mat)

# Patrol waypoints at body height.
waypoint_cls = unreal.load_class(None, '/Script/Arachne.ArachneWaypoint')
for i, (x, y) in enumerate([(-800, -400), (-750, 350), (-250, -350), (-300, 50), (-100, 0), (600, 0),
                            (1300, 0), (1500, -350), (1950, -350), (1900, 250), (1450, 350)]):
    w = actors.spawn_actor_from_class(waypoint_cls, unreal.Vector(x, y, 75), unreal.Rotator())
    w.set_actor_label('WP_Patrol_%02d' % i)

# Camp points: 80 cm from each surface. Arrow = where she faces.
camp_cls = unreal.load_class(None, '/Script/Arachne.ArachneCampPoint')
for label, loc, yaw in [('Camp_A_CornerNW', (-920, 520, 220), -45),
                        ('Camp_A_CornerSW', (-920, -520, 220), 45),
                        ('Camp_Corridor_Ceiling', (600, 0, 220), 180),
                        ('Camp_B_AboveDoor', (1280, 250, 220), 0),
                        ('Camp_B_CornerNE', (2120, 520, 220), -135),
                        ('Camp_B_CornerSE', (2120, -520, 220), 135)]:
    c = actors.spawn_actor_from_class(camp_cls, unreal.Vector(*loc), unreal.Rotator(yaw=yaw))
    c.set_actor_label(label)
    c.set_editor_property('preview_pawn_class', arachne_cls)
    if 'Corridor' in label:
        c.set_editor_property('surface_search_distance', 120.0)   # only the ceiling, not the corridor walls

spider = actors.spawn_actor_from_class(arachne_cls, unreal.Vector(-600, -100, 120), unreal.Rotator())
spider.set_actor_label('Arachne')
start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(1900, 0, 100), unreal.Rotator(yaw=180))
start.set_actor_label('Player Start')

# Dim interior light: the flashlight should matter.
for x in (-500, 600, 1700):
    light = actors.spawn_actor_from_class(unreal.PointLight, unreal.Vector(x, 0, 270))
    c = light.point_light_component
    c.set_editor_property('intensity', 2500.0)
    c.set_editor_property('attenuation_radius', 900.0)
    c.set_editor_property('light_color', unreal.Color(255, 205, 150, 255))
sky = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 600))
sky.light_component.set_editor_property('intensity', .3)

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
world.get_world_settings().set_editor_property('default_game_mode', gm_cls)
level.save_current_level()
unreal.log('ARACHNE_HUNT_SETUP_COMPLETE %s' % MAP_PATH)
