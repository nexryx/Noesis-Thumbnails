"""End-to-end axis checks using a built and configured Noesis bridge."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import uuid


POSITIONS = [(-1, -1, 0), (1, -1, 0), (0, 1, 1), (0, -1, -1)]
FACES = "f 1 2 3\nf 1 4 2\nf 1 3 4\nf 2 4 3\n"
COLLADA = """<?xml version="1.0" encoding="UTF-8"?>
<COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">
  <asset>
    <created>2026-01-01T00:00:00Z</created>
    <modified>2026-01-01T00:00:00Z</modified>
    <up_axis>{axis}</up_axis>
  </asset>
  <library_geometries>
    <geometry id="shape"><mesh>
      <source id="positions">
        <float_array id="values" count="12">{coordinates}</float_array>
        <technique_common><accessor source="#values" count="4" stride="3">
          <param name="X" type="float"/>
          <param name="Y" type="float"/>
          <param name="Z" type="float"/>
        </accessor></technique_common>
      </source>
      <vertices id="vertices"><input semantic="POSITION" source="#positions"/></vertices>
      <triangles count="4">
        <input semantic="VERTEX" source="#vertices" offset="0"/>
        <p>0 1 2 0 3 1 0 2 3 1 3 2</p>
      </triangles>
    </mesh></geometry>
  </library_geometries>
  <library_visual_scenes><visual_scene id="scene">
    <node id="node"><instance_geometry url="#shape"/></node>
  </visual_scene></library_visual_scenes>
  <scene><instance_visual_scene url="#scene"/></scene>
</COLLADA>
"""


def render_hash(worker, model, environment):
    bitmap = model.with_suffix(".bmp")
    subprocess.run(
        [str(worker), "thumbnail", str(model), str(bitmap)],
        env=environment, check=True, timeout=30,
    )
    return hashlib.sha256(bitmap.read_bytes()).hexdigest()


def write_obj(path, positions):
    vertices = "".join("v %s %s %s\n" % position for position in positions)
    path.write_text(vertices + FACES, encoding="utf-8")


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin-directory", type=Path, default=root / "dist")
    args = parser.parse_args()
    app = args.bin_directory.resolve()
    worker, config_path = app / "noesis-thumbnails.exe", app / "noesis-thumbnails.ini"
    if not worker.is_file() or not config_path.is_file():
        parser.error("Build and configure Noesis first; see docs/BUILD.md.")
    config = config_path.read_text(encoding="utf-16")
    executable = next((line.split("=", 1)[1] for line in config.splitlines()
                       if line.startswith("executable=")), "")
    if not executable:
        parser.error("No Noesis executable configured in " + str(config_path))
    noesis = Path(executable)
    if not noesis.is_absolute():
        noesis = app / noesis
    if not noesis.is_file():
        parser.error("Configured Noesis executable not found: " + str(noesis))

    output = root / ".local" / ("orientation-" + uuid.uuid4().hex)
    output.mkdir(parents=True)
    environment = dict(os.environ, NT_CACHE_DIR=str(output / "cache"))
    print("Test artifacts:", output, flush=True)
    hashes = []
    for axis in ("Y_UP", "Z_UP", "X_UP"):
        positions = [(x, y, z) if axis == "Y_UP" else
                     (x, -z, y) if axis == "Z_UP" else (y, -x, z)
                     for x, y, z in POSITIONS]
        coordinates = " ".join(str(value) for position in positions for value in position)
        model = output / (axis + ".dae")
        model.write_text(COLLADA.format(axis=axis, coordinates=coordinates), encoding="utf-8")
        hashes.append(render_hash(worker, model, environment))
    if len(set(hashes)) != 1:
        raise RuntimeError("DAE X/Y/Z previews differ")
    print("PASS: equivalent DAE X/Y/Z axes render identical pixels", flush=True)

    # The FBX switch declares the input convention. Export matching vertices.
    write_obj(output / "shape.obj", POSITIONS)
    write_obj(output / "shape-z.obj", [(x, -z, y) for x, y, z in POSITIONS])
    hashes = []
    for name, options in (("shape", []), ("shape-z", ["-fbxzup"])):
        model = output / (name + ".fbx")
        subprocess.run(
            [str(noesis), "?cmode", str(output / (name + ".obj")), str(model)] + options,
            cwd=str(noesis.parent), check=True, timeout=30,
            creationflags=subprocess.CREATE_NO_WINDOW,
        )
        hashes.append(render_hash(worker, model, environment))
    if len(set(hashes)) != 1:
        raise RuntimeError("FBX Y/Z previews differ")
    print("PASS: equivalent FBX Y/Z axes render identical pixels", flush=True)


if __name__ == "__main__":
    main()
