# Private integration-test format, installed only by tests/integration.ps1.
from inc_noesis import *
import time

def registerNoesisTypes():
    handle = noesis.register("Noesis thumbnail test triangle", ".nttest")
    noesis.setHandlerTypeCheck(handle, lambda data: 1 if data[:4] == b"NTST" else 0)
    noesis.setHandlerLoadModel(handle, load_model)
    return 1

def load_model(data, models):
    if b"HANG" in data:
        time.sleep(30)
    positions = [(-1, -1, 0), (1, -1, 0), (0, 1, 0)]
    if b"ZUP" in data:
        positions = [(x, -z, y) for x, y, z in positions]
        rapi.setPreviewOption("setAngOfs", "0 -90 0")
    mesh = NoeMesh([0, 1, 2], [NoeVec3(p) for p in positions], "triangle")
    if b"EMIT" in data:
        textures = [NoeTexture("black", 1, 1, bytes([0, 0, 0, 255]), noesis.NOESISTEX_RGBA32),
                    NoeTexture("cyan", 1, 1, bytes([0, 255, 255, 0]), noesis.NOESISTEX_RGBA32)]
        material = NoeMaterial("base", "black")
        emission = NoeMaterial("emission", "cyan")
        emission.setBlendMode("GL_ONE", "GL_ONE")
        emission.setDiffuseColor(NoeVec4((1, 1, 1, 1)))
        material.setNextPass(emission)
        mesh.setMaterial("base")
        mesh.setUVs([NoeVec3((0, 0, 0)) for p in positions])
        models.append(NoeModel([mesh], modelMats=NoeModelMaterials(textures, [material])))
        return 1
    models.append(NoeModel([mesh]))
    return 1
