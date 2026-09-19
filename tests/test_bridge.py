"""Bridge serialization regression tests; run with python -m unittest discover -s tests."""
import importlib.util
import pathlib
import struct
import sys
import tempfile
import types
import unittest
from unittest.mock import Mock, patch


class BridgeTests(unittest.TestCase):
    def setUp(self):
        api = Mock(NFORMATFLAG_MODELREAD=1, NFORMATFLAG_IMGREAD=2)
        api.getFormatExtensionFlags.return_value = 1
        rapi = Mock()
        rapi.toolGetLoadedModelCount.return_value = 1
        positions = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 2.0, 3.0)]
        mesh = types.SimpleNamespace(positions=positions, indices=[0, 1, 2],
                                     uvs=[(0.0, 0.0), (1.0, 0.0), (0.0, 1.0)], texRefIndex=-1)
        rapi.toolGetLoadedModel.return_value = types.SimpleNamespace(meshes=[mesh])
        rapi.loadMdlTextures.return_value = []
        module = types.ModuleType("inc_noesis")
        module.noesis, module.rapi = api, rapi
        path = pathlib.Path(__file__).resolve().parents[1] / "noesis/tool_noesis_thumbnails.py"
        spec = importlib.util.spec_from_file_location("bridge_under_test", path)
        bridge = importlib.util.module_from_spec(spec)
        with patch.dict(sys.modules, {"inc_noesis": module}):
            spec.loader.exec_module(bridge)
        self.bridge, self.api, self.rapi, self.mesh = bridge, api, rapi, mesh

    def test_reader_rotation_serialization_and_request_isolation(self):
        bridge, api, rapi, mesh = self.bridge, self.api, self.rapi, self.mesh
        positions = list(mesh.positions)
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / "scene.bin"
            # Follow a PSK with another format to catch orientation leaking
            # between requests handled by the same persistent Noesis worker.
            for suffix in (".psk", ".obj", ".PSK", ".fbx"):
                with self.subTest(suffix=suffix):
                    rotation = bridge.Z_UP if suffix.lower() == ".psk" else bridge.IDENTITY
                    with patch.object(bridge, "preview_rotation", return_value=rotation), patch.object(bridge, "append_materials") as materials:
                        bridge.export_scene("model" + suffix, str(output))
                    materials.assert_called_once_with(str(output), api.instantiateModule.return_value, [0, 3], rotation)
                    data = output.read_bytes()
                    self.assertEqual(struct.unpack_from("<4sIII", data), (b"NTS1", 1, 0, 1))
                    self.assertEqual(struct.unpack_from("<IIi", data, 16), (3, 3, -1))
                    expected = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 3.0, -2.0)] if suffix.lower() == ".psk" else positions
                    for i, pos in enumerate(expected):
                        vertex = struct.unpack_from("<5f", data, 28 + i * 20)
                        self.assertEqual(vertex[:3], pos)
                        self.assertEqual(vertex[3:], mesh.uvs[i])
                    self.assertEqual(struct.unpack_from("<3I", data, 88), (0, 1, 2))
                    self.assertEqual(len(data), 100)
            self.assertEqual(mesh.positions, positions)
            self.assertEqual(mesh.positions[2], (0.0, 2.0, 3.0))
        self.assertEqual(rapi.toolFreeGData.call_count, 4)
        self.assertEqual(api.freeModule.call_count, 4)

    def test_reader_rotation_takes_priority_over_format_fallback(self):
        # A reader may already have supplied a correction. Never rotate twice.
        for extension in (".psk", ".md2", ".md3", ".md5mesh", ".3ds", ".iqm", ".smd", ".ase", ".dae", ".fbx"):
            with self.subTest(extension=extension), patch.object(self.bridge, "preview_rotation", return_value=self.bridge.X_UP):
                self.assertEqual(self.bridge.model_rotation("missing" + extension, 1), self.bridge.X_UP)

    def test_missing_reader_rotation_uses_known_format_conventions(self):
        with patch.object(self.bridge, "preview_rotation", return_value=self.bridge.IDENTITY):
            for extension in (".smd", ".ASE"):
                self.assertEqual(self.bridge.model_rotation("model" + extension, 1), self.bridge.Z_UP)
            for extension in (".obj", ".stl", ".ply", ".gltf", ".glb", ".fbx"):
                self.assertEqual(self.bridge.model_rotation("model" + extension, 1), self.bridge.IDENTITY)

    def test_axis_rotations_preserve_handedness_and_up(self):
        transform = self.bridge.thumbnail_position
        self.assertEqual(transform((0, 0, 1), self.bridge.Z_UP), (0, 1, 0))
        self.assertEqual(transform((1, 0, 0), self.bridge.X_UP), (0, 1, 0))
        for rotation in (self.bridge.IDENTITY, self.bridge.Z_UP, self.bridge.X_UP):
            x, y, z = [transform(p, rotation) for p in ((1, 0, 0), (0, 1, 0), (0, 0, 1))]
            cross = (x[1]*y[2]-x[2]*y[1], x[2]*y[0]-x[0]*y[2], x[0]*y[1]-x[1]*y[0])
            self.assertEqual(cross, z)

    def test_collada_axes_namespaces_encodings_and_default(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "model.dae"
            for axis, expected in (("X_UP", self.bridge.X_UP), ("Y_UP", self.bridge.IDENTITY), ("Z_UP", self.bridge.Z_UP), (None, self.bridge.IDENTITY)):
                for encoding in ("utf-8", "utf-8-sig", "utf-16", "utf-16-be", "utf-16-le"):
                    with self.subTest(axis=axis, encoding=encoding):
                        element = "<c:up_axis> %s </c:up_axis>" % axis if axis else ""
                        xml = '<c:COLLADA xmlns:c="urn:collada"><!-- <asset><up_axis>Z_UP</up_axis></asset> --><c:asset><c:contributor><c:comments>not an axis</c:comments></c:contributor>%s</c:asset><c:library_geometries/></c:COLLADA>' % element
                        path.write_bytes(xml.encode(encoding))
                        with patch.object(self.bridge, "preview_rotation", return_value=self.bridge.IDENTITY):
                            self.assertEqual(self.bridge.model_rotation(str(path), 1), expected)
            path.write_text('<COLLADA><library_geometries><asset><up_axis>Z_UP</up_axis></asset></library_geometries></COLLADA>')
            self.assertEqual(self.bridge.collada_up_axis(str(path)), "Y_UP")

    def test_collada_cdata_and_numeric_character_reference(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "model.dae"
            for text in ("<![CDATA[Z_UP]]>", "Z&#95;UP", "Z&#x5f;UP"):
                path.write_text('<COLLADA><asset><up_axis>' + text + '</up_axis></asset></COLLADA>')
                self.assertEqual(self.bridge.collada_up_axis(str(path)), "Z_UP")

    def test_collada_rejects_bad_or_unbounded_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "model.dae"
            for content in ('<COLLADA><asset><up_axis>BAD</up_axis></asset></COLLADA>',
                            '<!DOCTYPE COLLADA [<!ENTITY axis "Z_UP">]><COLLADA><asset><up_axis>&axis;</up_axis></asset></COLLADA>',
                            '<COLLADA><asset><!--' + 'x' * (1024 * 1024), ''):
                path.write_text(content)
                with self.assertRaises(ValueError):
                    self.bridge.collada_up_axis(str(path))


if __name__ == "__main__":
    unittest.main()
