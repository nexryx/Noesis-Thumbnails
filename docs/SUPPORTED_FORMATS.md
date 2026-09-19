# Supported formats

The following **193 extensions** are detected by the bundled Noesis image and model readers. Extensions with an image reader are listed in the first group, even when they also support models. The available formats can vary with the Noesis runtime and installed plugins.

### Images and textures (74)

`.aia`, `.anim`, `.anm`, `.apti_tex`, `.art`, `.astc`, `.atlas`, `.bin`, `.bmp`, `.bntx`, `.btex`, `.cel`,
`.chasmfloors`, `.ctpk`, `.dat`, `.dcm`, `.dds`, `.dig`, `.dsg`, `.exr`, `.fla`, `.flc`, `.flh`, `.fli`,
`.flt`, `.ge64_img`, `.gif`, `.gim`, `.gnf`, `.gtx`, `.gxt`, `.hdr`, `.ico`, `.iff`, `.j2k`, `.jp2`,
`.jpeg`, `.jpg`, `.jps`, `.kmg`, `.lbm`, `.lmp`, `.m32`, `.m8`, `.mat`, `.mpo`, `.pcx`, `.pic`,
`.pkm`, `.png`, `.pvm`, `.pvp`, `.pvr`, `.rim`, `.rolwtex`, `.seg`, `.spr`, `.syt`, `.tdx`, `.ted`,
`.tex`, `.tga`, `.tim`, `.tm2`, `.tm3`, `.tspr`, `.uo_anim`, `.uo_art_tile`, `.uo_gump`, `.uo_map`,
`.uo_multi_tile`, `.uo_tex`, `.vtf`, `.wal`.

### Models and animations (119)

Extensions with a model or animation reader and no image reader:

`.3do`, `.3ds`, `.3o`, `.amd`, `.ani`, `.animlist`, `.apti_anm`, `.apti_mdl`, `.apti_msh`, `.apti_nrf`, `.ase`, `.bjp`,
`.bsp`, `.bvh`, `.car`, `.chasmmap`, `.cnt`, `.cpr`, `.czr`, `.dae`, `.dcmvolset`, `.dcr`, `.dct`, `.dff`,
`.dtt`, `.edm`, `.emc`, `.fbx`, `.fe`, `.ff11datset`, `.ff12a`, `.ff12m`, `.ff7ccmodel`, `.ff7ccmot`, `.ff9anm`, `.ff9mdl`,
`.ffa`, `.ffm`, `.fm`, `.ge64_anm`, `.ge64_bg`, `.ge64_mdl`, `.geo`, `.gfxbin`, `.ghb`, `.gla`, `.glb`, `.glm`,
`.gltf`, `.gmd`, `.gmi`, `.gmo`, `.iqm`, `.jkl`, `.job`, `.key`, `.kf`, `.kvx`, `.l62c`, `.las`,
`.lev`, `.lzs`, `.map`, `.md2`, `.md3`, `.md5anim`, `.md5mesh`, `.mdl`, `.mdr`, `.mdx`, `.mld`, `.mod`,
`.msh`, `.ndp3`, `.nif`, `.nii`, `.nj`, `.njm`, `.nmd`, `.noefbxmulti`, `.noelidar`, `.noeroomba`, `.noesis`, `.nud`,
`.obj`, `.pac`, `.pack`, `.ply`, `.ply2`, `.proc`, `.psa`, `.psk`, `.rb`, `.rda`, `.rdm`, `.rdmfrank`,
`.rolwmdl`, `.sam`, `.sbm`, `.sdf`, `.smd`, `.smsh`, `.stl`, `.t0mdl`, `.t0mot`, `.t0pak`, `.t3bpack`, `.tp`,
`.tpl`, `.tpr`, `.trb`, `.uasset`, `.umap`, `.vdf`, `.vox`, `.wad`, `.xfbin`, `.xgr`, `.ymo`.

Support depends on the file variant and available assets. Animation-only files need renderable geometry; archive-only readers are excluded. Some formats require companion assets or game-specific plugins. See the [build and development guide](BUILD.md) for rendering details and technical limitations.
