"""
Builds /VRR_Materials/PostProcess/M_PP_Film (post process, after tonemapping) and MI_PP_Film from the VRR nodes:

    UV       = UseVideoTape ? Vrr Tape Warp.UV : ViewportUV           (shared by every node that reads the scene)
    Picture  = Vrr Film Lens(UV)
    Picture  = UseVideoTape ? Vrr Tape Signal(Picture, UV, Glitch) : Picture
    Emissive = Vrr Film Grain( Picture + Vrr Halation(UV) )

Halation is added after the tape signal so its warm tint isn't replaced by the tape's smeared color.
Every node setting that matters is wired to a parameter, so MI_PP_Film can be tuned live.
Run from the editor's Python console (or Tools > Execute Python Script). Safe to re-run: it rebuilds the graph,
and MI_PP_Film keeps its overrides as long as the parameter names stay the same.
"""
import unreal

FOLDER = "/VRR_Materials/PostProcess"
MATERIAL_NAME = "M_PP_Film"
INSTANCE_NAME = "MI_PP_Film"

assets = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary


def load_or_create(name, asset_class, factory):
    path = f"{FOLDER}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.EditorAssetLibrary.load_asset(path)
    return assets.create_asset(name, FOLDER, asset_class, factory)


material = load_or_create(MATERIAL_NAME, unreal.Material, unreal.MaterialFactoryNew())
mel.delete_all_material_expressions(material)
material.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
material.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)


def node(expression_class, x, y):
    return mel.create_material_expression(material, expression_class, x, y)


def scalar(name, value, group, x, y):
    param = node(unreal.MaterialExpressionScalarParameter, x, y)
    param.set_editor_property("parameter_name", name)
    param.set_editor_property("default_value", value)
    param.set_editor_property("group", group)
    return param


def vector(name, value, group, x, y):
    param = node(unreal.MaterialExpressionVectorParameter, x, y)
    param.set_editor_property("parameter_name", name)
    param.set_editor_property("default_value", value)
    param.set_editor_property("group", group)
    return param


def tape_switch(x, y):
    # Several switch nodes with the same name share one value
    switch = node(unreal.MaterialExpressionStaticSwitchParameter, x, y)
    switch.set_editor_property("parameter_name", "UseVideoTape")
    switch.set_editor_property("default_value", True)
    switch.set_editor_property("group", "Video Tape")
    return switch


def connect(source, source_output, target, target_input):
    if not mel.connect_material_expressions(source, source_output, target, target_input):
        raise RuntimeError(f"Could not connect {source.get_name()}.{source_output} -> {target.get_name()}.{target_input}")


# Tape warp: the UV every scene-reading node uses
warp = node(unreal.MaterialExpressionVrrTapeWarp, -1500, -700)
connect(scalar("TapeLineJitter", 0.6, "Video Tape", -1900, -800), "", warp, "LineJitterIn")
connect(scalar("TapeGlitchChance", 0.25, "Video Tape", -1900, -700), "", warp, "GlitchChanceIn")
connect(scalar("TapeGlitchStrength", 24.0, "Video Tape", -1900, -600), "", warp, "GlitchStrengthIn")
connect(scalar("TapeHeadSwitch", 12.0, "Video Tape", -1900, -500), "", warp, "HeadSwitchShiftIn")

screen = node(unreal.MaterialExpressionScreenPosition, -1500, -450)
uv_switch = tape_switch(-1200, -600)
connect(warp, "UV", uv_switch, "True")
connect(screen, "ViewportUV", uv_switch, "False")

# Lens: chromatic aberration + softness
lens = node(unreal.MaterialExpressionVrrFilmLens, -800, -300)
connect(uv_switch, "", lens, "UV")
connect(scalar("ChromaticAberration", 0.004, "Lens", -1200, -300), "", lens, "ChromaticAberrationIn")
connect(scalar("Softness", 0.5, "Lens", -1200, -200), "", lens, "SoftnessIn")

# Tape signal: chroma bleed, scanlines, static, dropouts
signal = node(unreal.MaterialExpressionVrrTapeSignal, -500, -500)
connect(lens, "Color", signal, "Color")
connect(uv_switch, "", signal, "UV")
connect(warp, "Glitch", signal, "Glitch")
connect(scalar("TapeChromaBleed", 0.8, "Video Tape", -900, -800), "", signal, "ChromaBleedIn")
connect(scalar("ScanlineIntensity", 0.25, "Video Tape", -900, -700), "", signal, "ScanlineIntensityIn")
connect(scalar("TapeStatic", 0.03, "Video Tape", -900, -600), "", signal, "StaticIn")

picture_switch = tape_switch(-250, -350)
connect(signal, "Color", picture_switch, "True")
connect(lens, "Color", picture_switch, "False")

# Halation: red halo around highlights
halation = node(unreal.MaterialExpressionVrrHalation, -800, 100)
connect(uv_switch, "", halation, "UV")
connect(scalar("HalationThreshold", 0.7, "Halation", -1200, 0), "", halation, "ThresholdIn")
connect(scalar("HalationRadius", 12.0, "Halation", -1200, 100), "", halation, "RadiusIn")
connect(scalar("HalationIntensity", 0.6, "Halation", -1200, 200), "", halation, "IntensityIn")
connect(vector("HalationTint", unreal.LinearColor(1.0, 0.25, 0.08, 1.0), "Halation", -1200, 300), "", halation, "TintIn")

add = node(unreal.MaterialExpressionAdd, -100, 0)
connect(picture_switch, "", add, "A")
connect(halation, "Glow", add, "B")

# Grain on top of everything (screen space, not warped by the tape)
grain = node(unreal.MaterialExpressionVrrFilmGrain, 100, 0)
connect(add, "", grain, "Color")
connect(scalar("GrainAmount", 0.05, "Grain", -300, 450), "", grain, "AmountIn")
connect(scalar("GrainSize", 1.5, "Grain", -300, 550), "", grain, "SizeIn")

mel.connect_material_property(grain, "Color", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
mel.layout_material_expressions(material)
mel.recompile_material(material)
unreal.EditorAssetLibrary.save_loaded_asset(material)

instance = load_or_create(INSTANCE_NAME, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
mel.set_material_instance_parent(instance, material)
unreal.EditorAssetLibrary.save_loaded_asset(instance)
unreal.log(f"Built {FOLDER}/{MATERIAL_NAME} and {INSTANCE_NAME}")
