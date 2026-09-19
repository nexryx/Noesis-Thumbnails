"""Noesis 4.x tool bridge. Runs inside Noesis, never inside Explorer.

Python 3.2 compatible; install into Noesis/plugins/python.
The native broker owns the private work directory and process lifetime.
"""
from inc_noesis import *
import os
import time
import struct
import traceback
import ctypes
from html.parser import HTMLParser

MAX_VERTICES = 2000000
MAX_INDICES = 6000000
MAX_TEXTURE_EDGE = 1024


IDENTITY = (1, 0, 0, 0, 1, 0, 0, 0, 1)
Z_UP = (1, 0, 0, 0, 0, 1, 0, -1, 0)
X_UP = (0, -1, 0, 1, 0, 0, 0, 0, 1)


def preview_rotation(module):
    folder = noesis.getPluginsPath()
    if ctypes.sizeof(ctypes.c_void_p) == 8:
        folder = os.path.join(folder, "x64")
    native = ctypes.CDLL(os.path.join(folder, "noesis_thumbnails_formats.dll"))
    function = native.NT_GetPreviewRotation
    function.argtypes = (ctypes.c_int, ctypes.POINTER(ctypes.c_float), ctypes.c_int)
    function.restype = ctypes.c_int
    values = (ctypes.c_float * 9)()
    if not function(module, values, len(values)):
        raise ValueError("Noesis preview rotation unavailable; rebuild the native bridge")
    return tuple(values)


def collada_up_axis(path):
    # Noesis ships a trimmed Python runtime without pyexpat. Use the pure-Python
    # tokenizer for the document-level asset header; Noesis validates the model.
    # No entity expansion or external resources are involved.
    class HeaderComplete(Exception):
        pass

    class HeaderParser(HTMLParser):
        def __init__(self):
            HTMLParser.__init__(self)
            self.stack, self.text, self.axis = [], [], "Y_UP"

        def handle_starttag(self, tag, attrs):
            self.stack.append(tag.rsplit(":", 1)[-1])
            if len(self.stack) == 2 and self.stack[1] != "asset":
                raise HeaderComplete()

        def handle_endtag(self, tag):
            if self.stack == ["collada", "asset", "up_axis"]:
                self.axis = "".join(self.text).strip()
                if self.axis not in ("X_UP", "Y_UP", "Z_UP"):
                    raise ValueError("Invalid COLLADA up_axis")
            if self.stack == ["collada", "asset"]:
                raise HeaderComplete()
            if self.stack:
                self.stack.pop()

        def handle_data(self, value):
            if self.stack == ["collada", "asset", "up_axis"]:
                self.text.append(value)

        def handle_decl(self, declaration):
            raise ValueError("COLLADA DTDs are not supported")

        def unknown_decl(self, declaration):
            if declaration.startswith("CDATA["):
                self.handle_data(declaration[6:])
            else:
                raise ValueError("Unsupported COLLADA declaration")

        def handle_charref(self, name):
            value = int(name[1:], 16) if name.lower().startswith("x") else int(name)
            self.handle_data(chr(value))

    with open(path, "rb") as source:
        header = source.read(1024 * 1024)
    if header.startswith((b"\xff\xfe", b"\xfe\xff")):
        text = header.decode("utf-16", "replace")
    elif header[:2] == b"<\x00":
        text = header.decode("utf-16-le", "replace")
    elif header[:2] == b"\x00<":
        text = header.decode("utf-16-be", "replace")
    else:
        text = header.decode("utf-8-sig", "replace")
    parser = HeaderParser()
    try:
        parser.feed(text)
    except HeaderComplete:
        return parser.axis
    raise ValueError("Missing or oversized COLLADA asset header")


def model_rotation(path, module):
    rotation = preview_rotation(module)
    # Prefer the reader's preview transform: this also covers FBX axis metadata
    # and game-specific readers without guessing from geometry dimensions.
    if rotation != IDENTITY:
        return rotation
    extension = os.path.splitext(path)[1].lower()
    # These built-in readers leave Z-up geometry without a preview transform.
    if extension in (".smd", ".ase"):
        return Z_UP
    if extension == ".dae":
        return {"X_UP": X_UP, "Y_UP": IDENTITY, "Z_UP": Z_UP}[collada_up_axis(path)]
    return IDENTITY


def thumbnail_position(pos, rotation):
    # SDK basis vectors are rows for this preview transform. Rotate only the
    # exported vertices; the source asset, pose, UVs and winding stay intact.
    x, y, z = pos[0], pos[1], pos[2]
    r = rotation
    return (x*r[0] + y*r[1] + z*r[2], x*r[3] + y*r[4] + z*r[5], x*r[6] + y*r[7] + z*r[8])


def append_materials(output, module, selections, rotation):
    folder = noesis.getPluginsPath()
    if ctypes.sizeof(ctypes.c_void_p) == 8:
        folder = os.path.join(folder, "x64")
    native = ctypes.CDLL(os.path.join(folder, "noesis_thumbnails_formats.dll"))
    function = native.NT_AppendMaterials
    function.argtypes = (ctypes.c_int, ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int), ctypes.c_int, ctypes.POINTER(ctypes.c_float))
    function.restype = ctypes.c_int
    indices = (ctypes.c_int * len(selections))(*selections)
    matrix = (ctypes.c_float * 9)(*rotation)
    if not function(module, output, indices, len(selections) // 2, matrix):
        raise ValueError("Could not export Noesis material channels")


def registerNoesisTypes():
    noesis.registerTool("Noesis Thumbnails Worker", worker,
                        "Private background bridge for noesis-thumbnails")
    noesis.registerTool("Noesis Thumbnails Verify Formats", verify_formats,
                        "Validate format inventory including Python readers")
    return 1


def verify_formats(tool_index):
    path = noesis.getSelectedFile()
    if not path or not os.path.isfile(path):
        return 0
    module = noesis.instantiateModule()
    noesis.setModuleRAPI(module)
    try:
        with open(path, "r", encoding="utf-8") as f:
            candidates = set(line.split("\t")[0] for line in f)
        with open(path + ".verified", "w", encoding="utf-8") as f:
            for extension in sorted(candidates):
                flags = noesis.getFormatExtensionFlags(extension)
                if flags:
                    f.write(extension + "\t" + str(flags) + "\n")
            f.write("# NT_FORMATS_COMPLETE\n")
    finally:
        noesis.freeModule(module)
    return 0


def write_texture(out, tex):
    width, height = tex.width, tex.height
    if width < 1 or height < 1 or width > 32768 or height > 32768:
        raise ValueError("Invalid texture dimensions")
    rgba = tex.pixelData
    # loadImageRGBA / loadMdlTextures supply decompressed RGBA.
    scale = min(1.0, float(MAX_TEXTURE_EDGE) / max(width, height))
    if scale < 1.0:
        w, h = max(1, int(width * scale)), max(1, int(height * scale))
        rgba = rapi.imageResample(rgba, width, height, w, h)
        width, height = w, h
    if len(rgba) != width * height * 4:
        raise ValueError("Invalid RGBA data")
    out.write(struct.pack("<II", width, height))
    out.write(rgba)


def export_scene(path, output):
    module = noesis.instantiateModule()
    noesis.setModuleRAPI(module)
    loaded = False
    try:
        flags = 0
        name = os.path.basename(path)
        # Some game readers use compound suffixes, e.g. .mesh.1808312334.
        for index, character in enumerate(name):
            if character == ".":
                flags |= noesis.getFormatExtensionFlags(name[index:])
        if not (flags & (noesis.NFORMATFLAG_MODELREAD | noesis.NFORMATFLAG_IMGREAD)):
            raise ValueError("No installed Noesis image/model reader for this extension")
        with open(output, "wb") as out:
            # Noesis may also advertise MODELREAD for images (a texture-only model).
            # Prefer an actual image when an image handler accepts the file.
            if flags & noesis.NFORMATFLAG_IMGREAD:
                tex = noesis.loadImageRGBA(path)
                if tex is not None:
                    out.write(struct.pack("<4sIII", b"NTS1", 2, 1, 0))
                    write_texture(out, tex)
                    return
            if flags & noesis.NFORMATFLAG_MODELREAD:
                loaded = True
                if not rapi.toolLoadGData(path):
                    raise ValueError("Noesis could not load model")
                if rapi.toolGetLoadedModelCount() == 0:
                    raise ValueError("File has no model")
                # The first model is the representative thumbnail (no animations).
                model = rapi.toolGetLoadedModel(0)
                rotation = model_rotation(path, module)
                meshes = [m for m in model.meshes if len(m.positions) and len(m.indices)]
                selections = [value for i, mesh in enumerate(model.meshes) if len(mesh.positions) and len(mesh.indices) for value in (i, len(mesh.positions))]
                if not meshes:
                    raise ValueError("Model has no triangles")
                if sum(len(m.positions) for m in meshes) > MAX_VERTICES:
                    raise ValueError("Vertex budget exceeded")
                if sum(len(m.indices) for m in meshes) > MAX_INDICES:
                    raise ValueError("Triangle budget exceeded")
                textures = rapi.loadMdlTextures(model)
                if len(textures) > 256:
                    raise ValueError("Texture budget exceeded")
                out.write(struct.pack("<4sIII", b"NTS1", 1, len(textures), len(meshes)))
                for tex in textures:
                    write_texture(out, tex)
                for mesh in meshes:
                    tex_index = getattr(mesh, "texRefIndex", -1)
                    out.write(struct.pack("<IIi", len(mesh.positions), len(mesh.indices), tex_index))
                    for i, pos in enumerate(mesh.positions):
                        pos = thumbnail_position(pos, rotation)
                        uv = mesh.uvs[i] if i < len(mesh.uvs) else (0.0, 0.0)
                        out.write(struct.pack("<5f", pos[0], pos[1], pos[2], uv[0], uv[1]))
                    for index in mesh.indices:
                        out.write(struct.pack("<I", index))
                out.flush()
                append_materials(output, module, selections, rotation)
            else:
                raise ValueError("Noesis could not load image")
    finally:
        if loaded:
            rapi.toolFreeGData()
        noesis.freeModule(module)


def worker(tool_index):
    work = noesis.getSelectedFile()
    if not work or not os.path.isdir(work):
        return 0
    request = os.path.join(work, "request.txt")
    last_job = time.time()
    # The broker terminates this process on timeout or exit as an additional guard.
    while time.time() - last_job < 90:
        if os.path.exists(os.path.join(work, "stop")):
            break
        if not os.path.exists(request):
            time.sleep(0.025)
            continue
        try:
            with open(request, "r", encoding="utf-8") as f:
                path = f.read()
            os.remove(request)
            temporary = os.path.join(work, "scene.tmp")
            export_scene(path, temporary)
            os.rename(temporary, os.path.join(work, "scene.bin"))
        except Exception:
            with open(os.path.join(work, "error.tmp"), "w", encoding="utf-8") as f:
                f.write(traceback.format_exc())
            os.rename(os.path.join(work, "error.tmp"), os.path.join(work, "error.txt"))
        last_job = time.time()
    return 0
