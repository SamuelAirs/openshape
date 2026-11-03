# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Studio renders of the five sample projects in Blender (docs/MARKETING_RENDERS.md,
piece 8): each part on a seamless backdrop under soft lights, in a printed-plastic
colour, and the five together.

    python scripts/dev/marketing_blender.py [--only "Lamp shade"] [--preview | --turntable]

1. tools/export_bodies writes each sample's bodies as STL
   (build/marketing/blender/<sample>/).
2. Blender renders them (Cycles; the GPU when Blender finds one), this file
   running inside it: docs/marketing/blender/<sample>.png (1800 x 1800) and
   samples-family.png (2400 x 1350). --preview renders small and fast into
   build/marketing/blender/preview/ instead. --turntable renders each sample
   turning once (frames in build/marketing/blender/frames/, kept so a stopped
   run resumes) and encodes docs/marketing/blender/<sample>-turn.mp4
   (1080 x 1080, 8 s, 30 fps, H.264, loops seamlessly).

Needs: the build with OPENSHAPE_BUILD_TOOLS=ON (BUILDING.md), Blender 4.2 or
later (OPENSHAPE_BLENDER, or the newest ~/Tools/blender-*/blender.exe) and, for
--turntable, ffmpeg on PATH.
"""

import glob
import math
import os
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
EXPORT = os.path.join(ROOT, "build", "msys2-ucrt64", "bin", "export_bodies.exe")
SAMPLES = os.path.join(ROOT, "resources", "samples")
WORK = os.path.join(ROOT, "build", "marketing", "blender")
OUT = os.path.join(ROOT, "docs", "marketing", "blender")


def srgb(hex_color):
    return tuple(int(hex_color[i:i + 2], 16) / 255 for i in (1, 3, 5))


# Each sample: the colour of each body (in body order), how glossy it is, the
# backdrop, the camera (degrees around and above), how bright the studio
# lights are, and for the lamp shade how much light its wall lets through and
# the watts of a bulb inside.
LOOKS = {
    "Lamp shade": dict(bodies=["#F4EFE6"], roughness=0.55, translucent=0.4,
                       backdrop="#3A3632", view=(305, 18), bulb=3.0, key=0.18),
    "Twisted vase": dict(bodies=["#7FA387"], roughness=0.3, coat=0.25,
                         backdrop="#E9E6E1", view=(320, 24)),
    "Electronics box": dict(bodies=["#3B4049", "#2977ED"], roughness=0.45,
                            backdrop="#E6E8EB", view=(300, 42)),
    "Phone stand": dict(bodies=["#E8743B"], roughness=0.42,
                        backdrop="#ECE7E2", view=(295, 20)),
    "Honeycomb organizer": dict(bodies=["#F0B23C"], roughness=0.4,
                                backdrop="#EDE8E0", view=(325, 38)),
}
FAMILY = ["Electronics box", "Twisted vase", "Lamp shade", "Phone stand", "Honeycomb organizer"]
TURN_SECONDS, FPS = 8, 30  # --turntable: one full turn


# ---- Outside Blender: export the meshes, then run Blender on each sample ----------

def find_blender():
    if os.environ.get("OPENSHAPE_BLENDER"):
        return os.environ["OPENSHAPE_BLENDER"]
    found = sorted(glob.glob(os.path.join(os.path.expanduser("~"), "Tools", "blender-*", "blender.exe")))
    if not found:
        sys.exit("Blender not found: set OPENSHAPE_BLENDER to blender.exe")
    return found[-1]


def driver(argv):
    only = argv[argv.index("--only") + 1] if "--only" in argv else None
    preview = "--preview" in argv
    blender = find_blender()
    out = os.path.join(WORK, "preview") if preview else OUT
    os.makedirs(out, exist_ok=True)
    for name in LOOKS:
        meshes = os.path.join(WORK, name)
        for old in glob.glob(os.path.join(meshes, "*.stl")):
            os.remove(old)
        subprocess.run([EXPORT, os.path.join(SAMPLES, name + ".openshape"), meshes],
                       check=True, stdout=subprocess.DEVNULL)
    if "--turntable" in argv:
        for name in [n for n in LOOKS if only in (None, n)]:
            frames = os.path.join(WORK, "frames", slug(name))
            os.makedirs(frames, exist_ok=True)
            started = time.time()
            subprocess.run([blender, "--background", "--factory-startup", "--python", os.path.abspath(__file__),
                            "--", name, WORK, frames, "turn"], check=True)
            target = os.path.join(OUT, slug(name) + "-turn.mp4")
            subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(FPS),
                            "-i", os.path.join(frames, "%04d.png"), "-c:v", "libx264", "-preset", "slow",
                            "-crf", "18", "-pix_fmt", "yuv420p", "-movflags", "+faststart", target], check=True)
            print(f"{target}: {time.time() - started:.0f} s")
        return
    jobs = [n for n in LOOKS if only in (None, n)] + (["family"] if only in (None, "family") else [])
    for job in jobs:
        target = os.path.join(out, ("samples-family" if job == "family" else slug(job)) + ".png")
        started = time.time()
        subprocess.run([blender, "--background", "--factory-startup", "--python", os.path.abspath(__file__),
                        "--", job, WORK, target, "preview" if preview else "final"], check=True)
        print(f"{target}: {time.time() - started:.0f} s")


def slug(name):
    return name.lower().replace(" ", "-")


# ---- Inside Blender ---------------------------------------------------------------

def render_in_blender(job, work, output, quality):
    import bmesh
    import bpy
    from mathutils import Euler, Matrix, Vector

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene

    def linear(hex_color):
        def one(c):
            return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
        return tuple(one(c) for c in srgb(hex_color)) + (1.0,)

    def plastic(name, color, roughness, coat=0.0, translucent=0.0):
        material = bpy.data.materials.new(name)
        material.use_nodes = True
        nodes = material.node_tree.nodes
        bsdf = nodes["Principled BSDF"]
        bsdf.inputs["Base Color"].default_value = linear(color)
        bsdf.inputs["Roughness"].default_value = roughness
        bsdf.inputs["Coat Weight"].default_value = coat
        bsdf.inputs["Coat Roughness"].default_value = 0.08
        if translucent:  # a thin printed wall lets light through
            bsdf.inputs["Subsurface Weight"].default_value = translucent
            bsdf.inputs["Subsurface Radius"].default_value = (1.0, 0.8, 0.6)
            bsdf.inputs["Subsurface Scale"].default_value = 0.004
        # Printed edges are never knife-sharp: a render-only rounding catches the light.
        bevel = nodes.new("ShaderNodeBevel")
        bevel.samples = 8
        bevel.inputs["Radius"].default_value = 0.0004
        material.node_tree.links.new(bevel.outputs["Normal"], bsdf.inputs["Normal"])
        return material

    def import_body(path, material):
        bpy.ops.wm.stl_import(filepath=path, global_scale=0.001)  # mm -> m
        obj = bpy.context.selected_objects[0]
        mesh = obj.data
        bm = bmesh.new()
        bm.from_mesh(mesh)
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-7)
        bm.to_mesh(mesh)
        bm.free()
        mesh.shade_smooth()
        mesh.set_sharp_from_angle(angle=math.radians(32))
        mesh.materials.append(material)
        return obj

    def import_sample(name):
        look = LOOKS[name]
        objects = []
        for i, path in enumerate(sorted(glob.glob(os.path.join(work, name, "*.stl")))):
            color = look["bodies"][min(i, len(look["bodies"]) - 1)]
            material = plastic(f"{name} {i}", color, look["roughness"], look.get("coat", 0.0),
                               look.get("translucent", 0.0))
            objects.append(import_body(path, material))
        # One parent at the parts' footprint centre, standing on the floor.
        low = Vector((min(min((o.matrix_world @ Vector(c)).x for c in o.bound_box) for o in objects),
                      min(min((o.matrix_world @ Vector(c)).y for c in o.bound_box) for o in objects),
                      min(min((o.matrix_world @ Vector(c)).z for c in o.bound_box) for o in objects)))
        high = Vector((max(max((o.matrix_world @ Vector(c)).x for c in o.bound_box) for o in objects),
                       max(max((o.matrix_world @ Vector(c)).y for c in o.bound_box) for o in objects),
                       max(max((o.matrix_world @ Vector(c)).z for c in o.bound_box) for o in objects)))
        pivot = bpy.data.objects.new(name, None)
        scene.collection.objects.link(pivot)
        offset = Vector(((low.x + high.x) / 2, (low.y + high.y) / 2, low.z))
        for o in objects:
            o.location -= offset
            o.parent = pivot
        return pivot, objects, high - low

    def corners(objects):
        bpy.context.view_layer.update()
        return [o.matrix_world @ Vector(c) for o in objects for c in o.bound_box]

    def backdrop(color, facing, size):
        """A seamless cove: a floor curving up into a wall behind the parts."""
        floor, radius, wall, width = 30 * size, 4 * size, 30 * size, 60 * size
        profile = [(-floor, 0.0)]
        for i in range(25):
            a = math.pi / 2 * i / 24
            profile.append((radius * math.sin(a), radius * (1 - math.cos(a))))
        profile.append((radius, wall))
        bm = bmesh.new()
        rows = [[bm.verts.new((x, y, z)) for (y, z) in profile] for x in (-width / 2, width / 2)]
        for i in range(len(profile) - 1):
            bm.faces.new((rows[0][i], rows[1][i], rows[1][i + 1], rows[0][i + 1]))
        mesh = bpy.data.meshes.new("Backdrop")
        bm.to_mesh(mesh)
        bm.free()
        mesh.shade_smooth()
        obj = bpy.data.objects.new("Backdrop", mesh)
        scene.collection.objects.link(obj)
        # Its wall on the far side from the camera, 2.5 part sizes behind.
        obj.rotation_euler = Euler((0, 0, facing + math.pi / 2))
        back = Vector((-math.cos(facing), -math.sin(facing), 0))
        obj.location = back * 2.5 * size
        material = bpy.data.materials.new("Backdrop")
        material.use_nodes = True
        bsdf = material.node_tree.nodes["Principled BSDF"]
        bsdf.inputs["Base Color"].default_value = linear(color)
        bsdf.inputs["Roughness"].default_value = 0.9
        mesh.materials.append(material)

    def area_light(name, around, above, distance, size, power, target, color=(1, 1, 1)):
        light = bpy.data.lights.new(name, "AREA")
        light.shape = "RECTANGLE"  # a softbox: long soft reflections on glossy walls
        light.size, light.size_y = size, size * 1.6
        light.energy = power
        light.color = color
        obj = bpy.data.objects.new(name, light)
        scene.collection.objects.link(obj)
        direction = Vector((math.cos(above) * math.cos(around), math.cos(above) * math.sin(around), math.sin(above)))
        obj.location = target + direction * distance
        obj.rotation_euler = (-direction).to_track_quat("-Z", "Y").to_euler()

    def frame(camera, points, target, direction, lens, aspect, margin):
        """The distance along `direction` at which every point fits the frame."""
        camera.data.lens = lens
        half_w = math.atan(18 / lens) if aspect >= 1 else math.atan(18 / lens * aspect)
        half_h = math.atan(math.tan(half_w) / aspect)
        rotation = (-direction).to_track_quat("-Z", "Y").to_matrix()
        inverse = rotation.transposed()
        lo, hi = 0.01, 100.0
        for _ in range(60):
            d = (lo + hi) / 2
            eye = target + direction * d
            fits = True
            for p in points:
                v = inverse @ (p - eye)
                if -v.z <= 1e-6 or abs(v.x / -v.z) > math.tan(half_w) * margin \
                        or abs(v.y / -v.z) > math.tan(half_h) * margin:
                    fits = False
                    break
            hi, lo = (d, lo) if fits else (hi, d)
        camera.location = target + direction * hi
        camera.rotation_euler = rotation.to_euler()

    # The parts.
    if job == "family":
        width, height = (2400, 1350) if quality == "final" else (960, 540)
        placed, gap, x = [], 0.025, 0.0
        for name in FAMILY:
            pivot, objects, extent = import_sample(name)
            placed.append((pivot, objects, extent))
        total = sum(e.x for _, _, e in placed) + gap * (len(placed) - 1)
        x = -total / 2
        for i, (pivot, _, extent) in enumerate(placed):
            # A gentle arc: the middle part a little further back.
            pivot.location = Vector((x + extent.x / 2, 0.04 * math.sin(math.pi * (i + 0.5) / len(placed)), 0))
            x += extent.x + gap
        objects = [o for _, objs, _ in placed for o in objs]
        view, key, back_color = (270, 16), 1.0, "#E9E7E3"
    else:
        width, height = {"final": (1800, 1800), "turn": (1080, 1080)}.get(quality, (600, 600))
        pivot, objects, _ = import_sample(job)
        look = LOOKS[job]
        view, key, back_color = look["view"], look.get("key", 1.0), look["backdrop"]

    points = corners(objects)
    if quality == "turn":  # framed so that the parts fit at every angle of the turn
        points = [Matrix.Rotation(math.radians(a), 3, "Z") @ p for a in range(0, 360, 10) for p in points]
    low = Vector((min(p.x for p in points), min(p.y for p in points), min(p.z for p in points)))
    high = Vector((max(p.x for p in points), max(p.y for p in points), max(p.z for p in points)))
    target = (low + high) / 2
    size = max((high - low).length / 2, 0.02)
    around, above = math.radians(view[0]), math.radians(view[1])
    direction = Vector((math.cos(above) * math.cos(around), math.cos(above) * math.sin(around), math.sin(above)))

    camera = bpy.data.objects.new("Camera", bpy.data.cameras.new("Camera"))
    scene.collection.objects.link(camera)
    scene.camera = camera
    frame(camera, points, target, direction, 70 if job != "family" else 85, width / height,
          0.82 if job != "family" else 0.9)
    distance = (camera.location - target).length

    backdrop(back_color, around, size if job != "family" else size * 0.6)

    # Studio light: a soft key high to the camera's left, a weak fill to its right, a rim
    # behind; the backdrop falls off into shade away from the key.
    def lit(irradiance, distance):  # the power that lights the part this brightly from there
        return irradiance * key * math.pi * distance ** 2

    area_light("Key", around + math.radians(50), math.radians(52), 4 * size, 2.2 * size, lit(3.2, 4 * size), target)
    area_light("Fill", around - math.radians(70), math.radians(15), 6 * size, 5 * size, lit(0.45, 6 * size), target)
    area_light("Rim", around + math.radians(165), math.radians(55), 5 * size, 2.5 * size, lit(2.4, 5 * size), target)

    # An even grey room around the set: glossy parts reflect only the softboxes.
    world = bpy.data.worlds.new("World")
    scene.world = world
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.8, 0.8, 0.8, 1.0)
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.15 * key

    if job != "family" and LOOKS[job].get("bulb"):
        # A warm bulb in the shade's middle; its light falls through the windows.
        bulb = bpy.data.lights.new("Bulb", "POINT")
        bulb.energy = LOOKS[job]["bulb"]
        bulb.shadow_soft_size = 0.004
        bulb.color = (1.0, 0.72, 0.42)
        obj = bpy.data.objects.new("Bulb", bulb)
        scene.collection.objects.link(obj)
        obj.location = Vector((target.x, target.y, low.z + (high.z - low.z) * 0.45))

    # Cycles, on the GPU when there is one.
    scene.render.engine = "CYCLES"
    cycles = bpy.context.preferences.addons["cycles"].preferences
    device = "CPU"
    for kind in ("OPTIX", "CUDA", "HIP", "ONEAPI", "METAL"):
        try:
            cycles.compute_device_type = kind
        except TypeError:
            continue
        cycles.get_devices()
        gpus = [d for d in cycles.devices if d.type == kind]
        if gpus:
            for d in cycles.devices:
                d.use = d.type == kind
            device = kind
            break
    scene.cycles.device = "GPU" if device != "CPU" else "CPU"
    print(f"render device: {device}")
    scene.cycles.samples = {"final": 384, "turn": 160}.get(quality, 64)
    scene.cycles.adaptive_threshold = 0.01
    scene.cycles.use_denoising = True
    scene.cycles.use_animated_seed = False  # the same noise every frame: a turn does not shimmer
    scene.render.resolution_x, scene.render.resolution_y = width, height
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = False
    scene.view_settings.view_transform = "AgX"
    try:
        scene.view_settings.look = "AgX - Medium High Contrast"
    except TypeError:
        pass
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGB"
    print(f"camera {distance:.3f} m from a part {2 * size * 1000:.0f} mm across")
    if quality != "turn":
        scene.render.filepath = output
        bpy.ops.render.render(write_still=True)
        return
    # One full turn of the parts (the camera, lights and backdrop stay), a frame
    # per file; frames already there are kept, so a stopped turn resumes.
    scene.render.use_persistent_data = True
    count = TURN_SECONDS * FPS
    for f in range(count):
        path = os.path.join(output, f"{f + 1:04d}.png")
        if os.path.exists(path):
            continue
        pivot.rotation_euler.z = 2 * math.pi * f / count
        scene.render.filepath = path
        bpy.ops.render.render(write_still=True)


if __name__ == "__main__":
    try:
        import bpy  # noqa: F401 (only inside Blender)
    except ImportError:
        driver(sys.argv[1:])
    else:
        render_in_blender(*sys.argv[sys.argv.index("--") + 1:])
