"""
Builds /MaterialLibrary/PostProcess/M_PP_Film (post process, after tonemapping) and MI_PP_Film from the VRR nodes:

    UV       = UseVideoTape ? Vrr Tape Warp.UV : ViewportUV           (shared by every node that reads the scene)
    Picture  = Vrr Film Lens(UV, Channel Shift = Lens - ChannelShift + (UseVideoTape ? Vrr Tape Warp.Color Split : 0))
    Picture  = UseComicShade ? Vrr Comic Shade(Picture, UV) : Picture   (hatching + halftone dots)
    Picture  = UseVideoTape ? Vrr Tape Signal(Picture, UV, Glitch) : Picture
    Emissive = Vrr Film Grain( Picture + Vrr Halation(UV) )

Halation is added after the tape signal so its warm tint isn't replaced by the tape's smeared color.
Every node setting that matters is wired to a parameter, so MI_PP_Film can be tuned live.
Parameter names match the hand-edited graph ("Lens - ...", "Comic - ...", "Tape - ..."). Comic Shade's Tone input
is left unconnected so it uses the picture's luminance (a constant Tone turns the patterns off everywhere).
Note: the rebuild wires nodes directly. Named reroutes and hand layout are not recreated (Python cannot set a
reroute usage's declaration).
Run from the editor's Python console (or Tools > Execute Python Script). Safe to re-run: it rebuilds the graph,
and MI_PP_Film keeps its overrides as long as the parameter names stay the same.
"""
import unreal

FOLDER = "/MaterialLibrary/PostProcess"
MATERIAL_NAME = "M_PP_Film"
INSTANCE_NAME = "MI_PP_Film"
# Comic Shade dot style: HIGHLIGHTS (light dots on bright areas), PRINT (classic print ink dots, bigger the darker)
# or LIT_FALLOFF (Hi-Fi Rush: dots between shadow and highlight level, sized by brightness)
DOT_STYLE = "HIGHLIGHTS"

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


def scalar(name, value, group, x, y, slider_max=None):
    param = node(unreal.MaterialExpressionScalarParameter, x, y)
    param.set_editor_property("parameter_name", name)
    param.set_editor_property("default_value", value)
    param.set_editor_property("group", group)
    if slider_max is not None:
        param.set_editor_property("slider_max", slider_max)
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
connect(scalar("Tape - Color Split", 6.0, "Video Tape", -1900, -400), "", warp, "ColorSplitIn")

screen = node(unreal.MaterialExpressionScreenPosition, -1500, -450)
uv_switch = tape_switch(-1200, -600)
connect(warp, "UV", uv_switch, "True")
connect(screen, "ViewportUV", uv_switch, "False")

# Lens: chromatic aberration + softness
lens = node(unreal.MaterialExpressionVrrFilmLens, -800, -300)
connect(uv_switch, "", lens, "UV")
# Channel shift: a fixed shift, plus the red/blue wave inside glitch bands (only with the tape on)
split_switch = tape_switch(-1200, -450)
connect(warp, "Color Split", split_switch, "True")
connect(node(unreal.MaterialExpressionConstant, -1400, -400), "", split_switch, "False")
channel_shift = node(unreal.MaterialExpressionAdd, -1000, -450)
connect(scalar("Lens - ChannelShift", 0.0, "Lens", -1200, -500), "", channel_shift, "A")
connect(split_switch, "", channel_shift, "B")
connect(channel_shift, "", lens, "ChannelShift")
connect(scalar("Lens - ChromaticAberration", 0.004, "Lens", -1200, -300), "", lens, "ChromaticAberrationIn")
connect(scalar("Lens - Softness", 0.5, "Lens", -1200, -200), "", lens, "SoftnessIn")

# Comic shade: hatching on dark areas, halftone dots on bright areas
comic = node(unreal.MaterialExpressionVrrComicShade, -650, 0)
comic.set_editor_property("dot_style", getattr(unreal.VrrHalftoneStyle, DOT_STYLE))
connect(lens, "Color", comic, "Color")
connect(uv_switch, "", comic, "UV")
connect(scalar("Comic - ShadowLevel", 0.3, "Comic", -1000, -100), "", comic, "ShadowLevelIn")
connect(scalar("Comic - HighlightLevel", 0.7, "Comic", -1000, 0), "", comic, "HighlightLevelIn")
connect(scalar("Comic - HatchSpacing", 6.0, "Comic", -1000, 100), "", comic, "HatchSpacingIn")
connect(scalar("Comic - HatchOpacity", 0.5, "Comic", -1000, 200, slider_max=1.0), "", comic, "HatchOpacityIn")
connect(vector("Comic - InkColor", unreal.LinearColor(0.05, 0.04, 0.12, 1.0), "Comic", -1000, 300), "", comic, "InkColorIn")
connect(scalar("Comic - DotSpacing", 7.0, "Comic", -1000, 400), "", comic, "DotSpacingIn")
connect(scalar("Comic - DotOpacity", 0.35, "Comic", -1000, 500, slider_max=1.0), "", comic, "DotOpacityIn")
connect(vector("Comic - DotColor", unreal.LinearColor(1.0, 1.0, 1.0, 1.0), "Comic", -1000, 600), "", comic, "DotColorIn")
connect(scalar("Comic - HatchEndLevel", 0.3, "Comic", -1000, 700, slider_max=1.0), "", comic, "HatchEndLevelIn")
connect(scalar("Comic - DotShrinkDistance", 5000.0, "Comic", -1000, 800, slider_max=20000.0), "", comic, "DotShrinkDistanceIn")

comic_switch = node(unreal.MaterialExpressionStaticSwitchParameter, -400, 0)
comic_switch.set_editor_property("parameter_name", "UseComicShade")
comic_switch.set_editor_property("default_value", True)
comic_switch.set_editor_property("group", "Comic")
connect(comic, "Color", comic_switch, "True")
connect(lens, "Color", comic_switch, "False")

# Tape signal: chroma bleed, scanlines, static, dropouts
signal = node(unreal.MaterialExpressionVrrTapeSignal, -500, -500)
connect(comic_switch, "", signal, "Color")
connect(uv_switch, "", signal, "UV")
connect(warp, "Glitch", signal, "Glitch")
connect(scalar("Tape - ChromaBleed", 0.8, "Video Tape", -900, -800), "", signal, "ChromaBleedIn")
connect(scalar("Tape - ScanlineIntensity", 0.25, "Video Tape", -900, -700), "", signal, "ScanlineIntensityIn")
connect(scalar("Tape - Static", 0.03, "Video Tape", -900, -600), "", signal, "StaticIn")
# White streaks: rate everywhere, extra rate inside glitch bands, brightness, longest streak (fraction of screen width)
connect(scalar("Tape - Dropouts", 0.004, "Video Tape", -900, -1100), "", signal, "DropoutsIn")
connect(scalar("Tape - GlitchDropouts", 0.3, "Video Tape", -900, -1000), "", signal, "GlitchDropoutsIn")
connect(scalar("Tape - Dropout Intensity", 0.6, "Video Tape", -900, -900), "", signal, "DropoutIntensityIn")
connect(scalar("Tape - Dropout Length", 0.6, "Video Tape", -900, -1200), "", signal, "DropoutLengthIn")

picture_switch = tape_switch(-250, -350)
connect(signal, "Color", picture_switch, "True")
connect(comic_switch, "", picture_switch, "False")

# Halation: red halo around highlights
halation = node(unreal.MaterialExpressionVrrHalation, -800, 100)
connect(uv_switch, "", halation, "UV")
connect(scalar("HalationThreshold", 0.88, "Halation", -1200, 0), "", halation, "ThresholdIn")
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
