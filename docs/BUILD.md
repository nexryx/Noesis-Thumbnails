# Build and development

See [README.md](../README.md) for an introduction, installation instructions, and supported file formats.

## Build the installer

Run commands from the repository root in 64-bit PowerShell on Windows. Obtain `noesisv4474.zip` from the [Noesis author](https://www.richwhitehouse.com/noesis/) and place it in the repository root, then run:

```powershell
.\scripts\build-msi.ps1
```

To use an archive in another location:

```powershell
.\scripts\build-msi.ps1 -NoesisArchive 'D:\Tools\noesis.zip'
```

The archive must contain `Noesis64.exe` at its root and include `pluginsource.zip` for the SDK. Noesis and its SDK are not included in this source repository.

The script builds the C++17 native components, discovers supported formats, and creates `releases/NoesisThumbnails-<version>-x64.msi` and its `.sha256` checksum. The version is read from [`VERSION`](../VERSION), or can be overridden with `-Version`. Zig 0.14.1 and WiX 3.14.1 are downloaded and verified when needed.

## Test the installer

Noesis Thumbnails must be uninstalled before running the lifecycle test. The test temporarily installs the built MSI, checks its lifecycle, then uninstalls it:

```powershell
.\tests\msi-lifecycle.ps1
```

The test selects the installer using [`VERSION`](../VERSION). Use `-MsiPath` to test a different installer. Test logs are written under `build/`.

## Run regression tests

The build script runs the native renderer and cache tests. With Python installed, run the bridge serialization and orientation tests:

```powershell
python -m unittest discover -s tests
```

After building, use an existing extracted Noesis runtime to test the bridge. Replace the example path with your runtime directory; the integration script configures the local build before running:

```powershell
.\tests\integration.ps1 -NoesisPath 'D:\Tools\Noesis'
python tests/orientation.py
```

## Render a local preview

After building, configure an existing Noesis installation and render a preview without registering an Explorer handler:

```powershell
.\scripts\configure.ps1 -NoesisPath 'D:\Tools\Noesis'
.\dist\noesis-thumbnails.exe thumbnail 'D:\Models\model.obj' preview.bmp
```

Configuration installs the bridge and format inventory plugin into the selected Noesis installation. Rerun the configuration script after changing Noesis plugins to invalidate the thumbnail cache. Cache and application logs are stored in `%LOCALAPPDATA%\NoesisThumbnails`.

## Repository layout

| Folder | Contents |
| --- | --- |
| `docs/` | Build guide, supported formats, and changelog |
| `src/` | Native provider, worker, cache, and renderer |
| `noesis/` | Noesis bridge and format discovery plugin |
| `installer/` | MSI definition, custom actions, and notices |
| `scripts/` | Build and development utilities |
| `tests/` | Automated tests with generated fixtures |
| `releases/` | MSI and checksum for GitHub release attachments; Git-ignored |

`dist/`, `build/`, `.local/`, and `.tools/` are generated on demand and Git-ignored. The local Noesis ZIP is a build input, also Git-ignored. Upload the source to the repository and attach the MSI/checksum separately to a GitHub release.

## Development scripts

| Script | Purpose |
| --- | --- |
| `build-msi.ps1` | Build and package the release installer |
| `build.ps1` | Compile native components and run native tests |
| `configure.ps1` | Connect a local build to a Noesis installation |
| `discover-formats.ps1` | Generate the format inventory used by packaging and registration |
| `register.ps1`, `unregister.ps1` | Register a development build and restore prior handlers; also used by integration tests |
| `install.ps1` | Set up a development build through the build, configuration, and registration scripts |
| `prewarm.ps1` | Generate thumbnails for a folder using the development build |
| `clear-cache.ps1` | Clear generated thumbnail data during local development |

These scripts support development from the repository. Normal installation and removal use the MSI and Windows Settings.

## Technical limitations

Model thumbnails use the preview rotation supplied by their Noesis reader, including PSK, Quake formats, 3DS, IQM, and FBX axis metadata. When the reader provides no rotation, SMD/ASE use Z-up and DAE uses its document-level `up_axis` (X, Y, or Z; Y by default). This changes only the preview, not the source file or its pose. The native format inventory DLL also supplies the preview rotation API, so update it together with the Python bridge.

Formats without an axis declaration or a reader-provided rotation retain their imported coordinates. An arbitrary OBJ/STL/PLY export has no universally inferable top or front; upright orientation is separate from pose and camera yaw.

Registered extensions do not guarantee every file variant works. Archives, animation-only files, missing companion assets, and interactive plugins may not produce previews. Rendering uses the first model/image. The CPU renderer supports base textures, PBR normal/specular maps and channel swizzles, environment reflections, and an additive emissive material pass. Its lighting is an approximation: it does not reproduce all Noesis shaders, HDR environment filtering, material layers, or transparency effects. External texture changes alone do not invalidate the cache. Network paths and unhydrated cloud files are skipped.
