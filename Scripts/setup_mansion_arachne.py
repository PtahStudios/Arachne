"""
ARACHNE on L_MansionWhitebox - run inside the Unreal Editor AFTER compiling the C++ changes:
    Tools > Execute Python Script...  (or console:  py "<project>/Scripts/setup_mansion_arachne.py")

Places everything Arachne needs on the mansion whitebox, from its manifest (exact room and door data):
  - patrol destinations in every room (orange)
  - an ArachneDoorway in every door frame (crossing manoeuvre: line up, straight through the middle, carry on)
  - helper points on both sides of every doorway, along both staircases and around the first-floor gallery (grey)
  - camp points in room corners under the ceiling: the corner nearest a door (ambush) and the opposite one (pink)
  - BP_Arachne in the basement hall if the map has none, BP_GM_Arachne as the map's game mode
Old Arachne waypoints / camp points in the map are removed first. Other actors are not touched.
Finally it checks the waypoint graph with the same rules as the game (clear for the body, ground underneath,
not steeper than stairs) and writes Saved/ArachneMansionReport.json.
"""
import unreal, json, math, os
from pathlib import Path

MANIFEST = r'C:\Users\ptahs\Desktop\ART\Personal\ARACHNE\MansionWhitebox\manifest.json'
MAP = '/Game/ARACHNE/Levels/L_MansionWhitebox'
BPS = '/Game/ARACHNE/Blueprints'
BODY = 75.0            # waypoint height above the floor (cm) = Arachne BodyHeight
CAMP_OFFSET = 80.0     # camp point distance from each wall (cm); the pose itself is solved in C++ (BodyHeight clearance)
CAMP_BELOW_CEILING = 30.0   # camp actor / wall probe height under the ceiling: above every door and window opening
LINK_DISTANCE = 1800.0
WALK_RADIUS = 35.0

manifest = json.loads(Path(MANIFEST).read_text())
level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level_subsystem.load_level(MAP), 'could not open ' + MAP
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()

def U(x, y, z):
    """Blender metres (+Y north) -> Unreal centimetres (the import flipped Y)."""
    return unreal.Vector(x * 100.0, -y * 100.0, z * 100.0)

VIS = unreal.TraceTypeQuery.TRACE_TYPE_QUERY1
NONE = unreal.DrawDebugTrace.NONE

def _hit_location(hit):
    try:
        parts = unreal.GameplayStatics.break_hit_result(hit)
    except Exception:
        parts = hit.to_tuple()
    vectors = [v for v in parts if isinstance(v, unreal.Vector)]
    return vectors[0] if vectors else None

def trace(a, b):
    hit = unreal.SystemLibrary.line_trace_single(world, a, b, VIS, False, [], NONE, True)
    return _hit_location(hit) if hit is not None else None

def sweep(a, b, radius):
    return unreal.SystemLibrary.sphere_trace_single(world, a, b, radius, VIS, False, [], NONE, True) is not None

def V(x, y, z):
    return unreal.Vector(x, y, z)

def ground_z(p, up=120.0, down=260.0):
    hit = trace(V(p.x, p.y, p.z + up), V(p.x, p.y, p.z - down))
    return hit.z if hit else None

def indoor(p):
    """Floor within reach below and a ceiling somewhere above (outside has no ceiling)."""
    return trace(p, V(p.x, p.y, p.z - 160)) is not None and trace(p, V(p.x, p.y, p.z + 900)) is not None

def free(p, radius=30.0):
    return not sweep(p, V(p.x, p.y, p.z + 1.0), radius)

def floor_point(x, y, z_hint, quiet=False):
    """Blender (x, y, surface z guess) -> UE point BODY above the actual surface, nudged out of geometry. None if invalid."""
    base = U(x, y, z_hint)
    g = ground_z(base)
    if g is None:
        return None
    p = V(base.x, base.y, g + BODY)
    if free(p) and indoor(p):
        return p
    for dx, dy in [(60, 0), (-60, 0), (0, 60), (0, -60), (60, 60), (-60, -60), (60, -60), (-60, 60)]:
        q = V(p.x + dx, p.y + dy, p.z)
        if free(q) and indoor(q):
            return q
    return None

# ------------------------------------------------------------------------------------------------ clean up
for a in actors.get_all_level_actors():
    if isinstance(a, unreal.ArachneWaypoint):   # camp points are waypoints too
        actors.destroy_actor(a)

waypoint_cls = unreal.ArachneWaypoint.static_class()
doorway_cls = unreal.ArachneDoorway.static_class()
camp_cls = unreal.ArachneCampPoint.static_class()
arachne_cls = unreal.EditorAssetLibrary.load_blueprint_class(BPS + '/BP_Arachne')
gm_cls = unreal.EditorAssetLibrary.load_blueprint_class(BPS + '/BP_GM_Arachne')
assert arachne_cls and gm_cls, 'run Scripts/setup_hunt.py once first (creates the blueprints)'

points = []   # dicts: label, pos, destination, kind
problems = []

def add_point(label, p, destination, kind, yaw=0.0):
    if p is None:
        problems.append('no valid spot for ' + label)
        return
    for q in points:
        if (q['pos'] - p).length() < 50.0:
            if kind == 'doorway' and q['kind'] != 'doorway':
                points.remove(q)   # a door frame always wins over a helper point
                break
            return   # duplicate (neighbouring doors / stair ends)
    points.append({'label': label, 'pos': p, 'destination': destination, 'kind': kind, 'yaw': yaw})

# ------------------------------------------------------------------------------------------------ rooms
for r in manifest['rooms']:
    x, y, z = r['center']
    add_point('WP_%s_%s' % (r['level'], r['name']), floor_point(x, y, z), True, 'room')

# ------------------------------------------------------------------------------------------------ doors
for i, d in enumerate(manifest['doors']):
    # axis 'x': wall runs along X at y = fixed, opening centred at x = center; axis 'y': wall along Y at x = fixed.
    if d['axis'] == 'x':
        cx, cy, nx, ny = d['center'], d['fixed'], 0.0, 1.0
    else:
        cx, cy, nx, ny = d['fixed'], d['center'], 1.0, 0.0
    z = d['z']
    tag = '%s_%02d' % (d['group'], i)
    # Door frame: arrow through the opening (Blender axis 'x' wall -> normal along Y -> UE yaw 90).
    add_point('Doorway_%s' % tag, floor_point(cx, cy, z), False, 'doorway', 90.0 if d['axis'] == 'x' else 0.0)
    for side, s in (('A', 1.0), ('B', -1.0)):
        p = floor_point(cx + nx * .9 * s, cy + ny * .9 * s, z)
        if p is None and d['group'] in ('GF_South', 'GF_North'):
            continue   # outside of an entrance door: nothing to patrol there
        add_point('WP_Door_%s_%s' % (tag, side), p, False, 'door')

# ------------------------------------------------------------------------------------------------ stairs (Blender coords, surface z guess)
def flight_up(y, y0, z0, rise, run=3.6):
    return z0 + rise * (y - y0) / run
def flight_down(y, y0, z0, rise, run=3.6):
    return z0 + rise * (y0 - y) / run

grand = [(0, 1.2, 0), (0, 2.6, flight_up(2.6, 1.7, 0, 2.1)), (0, 4.4, flight_up(4.4, 1.7, 0, 2.1)),
         (0, 6.3, 2.1), (-3.2, 6.3, 2.1), (3.2, 6.3, 2.1)]
for sx in (-3.2, 3.2):
    grand += [(sx, 4.4, flight_down(4.4, 5.3, 2.1, 2.1)), (sx, 2.6, flight_down(2.6, 5.3, 2.1, 2.1)), (sx, 1.2, 4.2)]
for i, (x, y, z) in enumerate(grand):
    add_point('WP_GrandStair_%02d' % i, floor_point(x, y, z), False, 'stairs')

service = []
for base, rise in ((-2.8, 2.8), (0.0, 4.2)):
    half = rise / 2
    service += [(7.25, 5.0, base), (7.25, 6.6, flight_up(6.6, 5.7, base, half)), (7.25, 8.4, flight_up(8.4, 5.7, base, half)),
                (7.25, 9.95, base + half), (8.75, 9.95, base + half),
                (8.75, 8.4, base + half + flight_down(8.4, 9.3, 0, half)), (8.75, 6.6, base + half + flight_down(6.6, 9.3, 0, half)),
                (8.75, 5.0, base + rise)]
service.append((7.25, 5.0, 4.2))
for i, (x, y, z) in enumerate(service):
    add_point('WP_ServiceStair_%02d' % i, floor_point(x, y, z), False, 'stairs')

# First-floor gallery ring around the hall void.
gallery = [(4.26, -4.9), (4.26, -2.0), (4.26, 1.0), (5.41, 4.5), (5.41, 7.9),
           (-4.26, -4.9), (-4.26, -2.0), (-4.26, 1.0), (-5.3, 4.5), (-5.3, 7.9), (0.0, 8.9)]
for i, (x, y) in enumerate(gallery):
    add_point('WP_Gallery_%02d' % i, floor_point(x, y, 4.2), False, 'gallery')

# ------------------------------------------------------------------------------------------------ spawn waypoints
spawned = []
for p in points:
    is_door = p['kind'] == 'doorway'
    a = actors.spawn_actor_from_class(doorway_cls if is_door else waypoint_cls, p['pos'], unreal.Rotator(yaw=p['yaw']))
    a.set_actor_label(p['label'])
    a.set_folder_path('Arachne/Waypoints/' + ('Doorways' if is_door else 'Rooms' if p['destination'] else 'Helpers'))
    a.set_editor_property('patrol_destination', p['destination'])
    a.set_editor_property('auto_link_distance', LINK_DISTANCE)
    if not p['destination']:
        a.set_editor_property('wait_time_min', 0.0)
        a.set_editor_property('wait_time_max', 0.0)
    spawned.append(a)

# ------------------------------------------------------------------------------------------------ camp points
door_centres = []
for d in manifest['doors']:
    cx, cy = (d['center'], d['fixed']) if d['axis'] == 'x' else (d['fixed'], d['center'])
    door_centres.append(U(cx, cy, d['z']))

camps = []
for r in manifest['rooms']:
    x, y, z = r['center']
    centre = floor_point(x, y, z)
    if centre is None:
        continue
    floor_z = centre.z - BODY
    ceiling = trace(centre, V(centre.x, centre.y, centre.z + 600))
    if ceiling is None or ceiling.z - floor_z > 450:
        problems.append('camp: skipped %s_%s (open to the floor above)' % (r['level'], r['name']))
        continue
    cz = ceiling.z - CAMP_BELOW_CEILING
    probe = V(centre.x, centre.y, cz)
    walls = {}
    for key, d in (('x+', V(1, 0, 0)), ('x-', V(-1, 0, 0)), ('y+', V(0, 1, 0)), ('y-', V(0, -1, 0))):
        hit = trace(probe, V(probe.x + d.x * 900, probe.y + d.y * 900, cz))
        if hit:
            walls[key] = hit
    if len(walls) < 4:
        problems.append('camp: skipped %s_%s (room not closed at ceiling height)' % (r['level'], r['name']))
        continue
    x0, x1 = walls['x-'].x + CAMP_OFFSET, walls['x+'].x - CAMP_OFFSET
    y0, y1 = walls['y-'].y + CAMP_OFFSET, walls['y+'].y - CAMP_OFFSET
    if x1 <= x0 or y1 <= y0:
        continue
    corners = [V(cx_, cy_, cz) for cx_ in (x0, x1) for cy_ in (y0, y1)]
    corners = [c for c in corners if free(c, 25.0) and trace(probe, c) is None]
    if not corners:
        continue
    near_doors = [dc for dc in door_centres if abs(dc.z - floor_z) < 100]
    def door_distance(c):
        return min([(V(c.x, c.y, 0) - V(dc.x, dc.y, 0)).length() for dc in near_doors] or [1e9])
    corners.sort(key=door_distance)
    chosen = [corners[0]]
    area = (x1 - x0 + 2 * CAMP_OFFSET) * (y1 - y0 + 2 * CAMP_OFFSET) / 10000.0
    if area > 12.0:
        opposite = V(x0 + x1 - corners[0].x, y0 + y1 - corners[0].y, cz)
        if any((opposite - c).length() < 1 for c in corners):
            chosen.append(opposite)
    for i, c in enumerate(chosen):
        yaw = math.degrees(math.atan2(centre.y - c.y, centre.x - c.x))
        a = actors.spawn_actor_from_class(camp_cls, c, unreal.Rotator(yaw=yaw))
        a.set_actor_label('Camp_%s_%s_%s' % (r['level'], r['name'], 'Door' if i == 0 else 'Far'))
        a.set_folder_path('Arachne/Camps')
        a.set_editor_property('preview_pawn_class', arachne_cls)
        camps.append(a)

# ------------------------------------------------------------------------------------------------ Arachne + game mode
if not [a for a in actors.get_all_level_actors() if isinstance(a, unreal.ArachnePawn)]:
    spot = floor_point(0, 0, -2.8)   # basement hall
    spider = actors.spawn_actor_from_class(arachne_cls, V(spot.x, spot.y, spot.z + 45), unreal.Rotator())
    spider.set_actor_label('Arachne')
    spider.set_folder_path('Arachne')
world.get_world_settings().set_editor_property('default_game_mode', gm_cls)

# ------------------------------------------------------------------------------------------------ graph check (same rules as C++)
def walkable(a, b):
    rise, run = abs(a.z - b.z), math.hypot(a.x - b.x, a.y - b.y)
    if rise > max(60.0, run * .9):
        return False
    if sweep(a, b, WALK_RADIUS):
        return False
    n = max(1, int(math.ceil((b - a).length() / 60.0)))
    for i in range(1, n):
        t = i / n
        p = V(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t)
        if trace(p, V(p.x, p.y, p.z - 160)) is None:
            return False
    return True

N = len(points)
links = [[] for _ in range(N)]
for i in range(N):
    for j in range(i + 1, N):
        a, b = points[i]['pos'], points[j]['pos']
        if (a - b).length() <= LINK_DISTANCE and walkable(a, b):
            links[i].append(j)
            links[j].append(i)

component = [-1] * N
components = []
for s in range(N):
    if component[s] != -1:
        continue
    stack, members = [s], []
    component[s] = len(components)
    while stack:
        k = stack.pop()
        members.append(k)
        for m in links[k]:
            if component[m] == -1:
                component[m] = len(components)
                stack.append(m)
    components.append(members)
components.sort(key=len, reverse=True)

report = {
    'map': MAP,
    'waypoints': N,
    'destinations': sum(1 for p in points if p['destination']),
    'doorways': sum(1 for p in points if p['kind'] == 'doorway'),
    'camp_points': len(camps),
    'links': sum(len(l) for l in links) // 2,
    'graph_components': len(components),
    'main_component_size': len(components[0]) if components else 0,
    'disconnected': [[points[k]['label'] for k in c] for c in components[1:]],
    'isolated': [points[k]['label'] for k in range(N) if not links[k]],
    'problems': problems,
}
out = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), 'ArachneMansionReport.json')
with open(out, 'w') as f:
    json.dump(report, f, indent=2)
level_subsystem.save_current_level()
unreal.log('ARACHNE_MANSION_SETUP %s' % json.dumps({k: report[k] for k in ('waypoints', 'camp_points', 'links', 'graph_components')}))
