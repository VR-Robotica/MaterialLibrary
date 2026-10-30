# Material Library

Stylized material nodes and ready-to-use masters for Unreal Engine 5.8: toon shading with its own shadow map,
cel and blob shadows, comic print and film / video tape post processing, dissolve and hit flash, vertex animation
texture (VAT) materials and a set of PBR material functions.

Free to use under the MIT License (see [LICENSE](LICENSE)).

Supported engine: **5.8** (the content is saved in 5.8 and can't be opened in older versions). Platforms: Windows,
macOS, Linux. Requires the engine plugins **AnimToTexture** (VAT functions) and **BaseMaterial** (M_Generic); both
ship with the engine and are enabled with this plugin.

## Material nodes

Right-click in any material graph and look under **VRR**. Each node is written in C++, so it compiles to the same
HLSL everywhere and shows clear errors when an input is missing.

| Node | What it does |
|---|---|
| Vrr Toon Shading | Cel shading from one art-directed fake light (scene lights are ignored). Outputs Emissive for Unlit materials, with band count, rim, specular and metallic bands. |
| Vrr Toon Shadow | Cast shadows for toon materials, read from the Vrr Toon Shadow Caster's shadow map (1 = lit, 0 = shadow). |
| Vrr Blob Shadow | A cel-shaded blob shadow projected onto the ground under a character (used by M_Toon_BlobShadow). |
| Vrr Cel Shadow | Post process: a flat drop shadow behind meshes marked with the Vrr Cel Layer component. |
| Vrr Comic Shade | Post process: comic print shading with hatching and halftone dots (Highlights, Print or Lit Falloff dot styles). |
| Vrr Film Grain | Post process: soft, clumpy grain that changes 24 times a second. |
| Vrr Film Lens | Post process: chromatic aberration, channel shift and softness toward the edges. |
| Vrr Halation | Post process: the warm halo film gets around bright highlights. |
| Vrr Tape Grade / Tape Signal / Tape Warp | Post process: the color, signal flaws and picture wobble of a worn 1990s video tape. |
| Vrr Dissolve | Noise-driven dissolve with a glowing burn edge. |
| Vrr Hit Flash | An emissive flash that fades out after a hit. Gameplay only writes the hit time to a parameter. |
| Vrr Stepped Time | Time that advances in held steps, for animation "on twos" or "on threes". |
| Vrr UV Region | Picks one material region out of a packed "UV Layouts" channel (atlas or UDIM). |

<img width="630" height="600" alt="AnimeToonShader_Test01" src="https://github.com/user-attachments/assets/4045c527-a4f6-49df-9925-96286a71e4ce" />

## Components and actors

Add these to your actors, or place them in the level:

- **Vrr Toon Shadow Caster** (actor): renders the depth map Vrr Toon Shadow reads, looking along the toon light set
  in `MPC_ToonLighting`. Place one per level that uses toon shadows.
- **Vrr Toon Boil** (component) and **Project Settings > VRR Toon Boil**: the toon noise "boils" only while the
  camera or an object moves, like hand-drawn animation where held frames stay still.
- **Vrr Blob Shadow** (component): keeps a blob shadow under its owner, optionally following the feet.
- **Vrr Cel Layer** (component): marks its owner's meshes for the Vrr Cel Shadow post process. Needs
  **Project Settings > Rendering > Custom Depth-Stencil Pass = Enabled with Stencil**.

## Content

All content lives under **Plugins > Material Library Content** in the Content Browser.

- `Masters/Toon`: `M_Toon_Base`, `M_Toon_Floor`, `M_Toon_Outline` (inverted hull), `M_Toon_BlobShadow`, the shared
  `MF_ToonLit` function and `MPC_ToonLighting` (light direction and colors for every toon material).
- `Masters`: `M_BasicColor`, `M_BasicColor_VAT`, `M_Generic` (triplanar, macro variation), `M_Glass`.
- `PostProcess`: `PPM_CelShadow` (place at Scene Color Before DOF) and `PPM_Film`, with parameter functions for
  each effect.
- `Functions/VAT_v2`: vertex and bone VAT playback with up to 11 animation slots (`ML_VAT` holds the parameters).
- `Functions`: PBR channel functions (base color, normal, roughness, metallic, AO), detail normals, tiling and
  material layers.

## Support

Questions and bug reports: [GitHub issues](https://github.com/VR-Robotica/MaterialLibrary/issues).
