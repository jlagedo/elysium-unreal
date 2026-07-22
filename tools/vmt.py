"""Tiny VMT (Valve Material) parser - just what we need for rendering.

VMTs are a keyvalues text file. We extract $basetexture and a few flags.
Some VMTs use the "patch" shader that `include`s another VMT; we follow one
level of that.
"""
import re

def _decomment(text):
    """Strip KeyValues `//` line comments, honouring quotes. A key inside a fully
    commented line (`//"$selfillum" "1"`) must not be read; VMT values are always
    quoted, so a `//` outside quotes always starts a comment."""
    out = []
    for line in text.splitlines():
        inq, cut, i = False, None, 0
        while i < len(line):
            c = line[i]
            if c == '"':
                inq = not inq
            elif c == '/' and not inq and i + 1 < len(line) and line[i + 1] == '/':
                cut = i; break
            i += 1
        out.append(line if cut is None else line[:cut])
    return "\n".join(out)

def _find(text, key):
    # trailing "//..." is a KeyValues line comment (VtMB's patch VMTs annotate
    # values, e.g. `"$basetexture" "..."  // fixed by psycho-a`); allow it.
    m = re.search(r'"?\$%s"?\s+"?([^"\r\n]+?)"?\s*(?://[^\r\n]*)?\s*$' % key, text,
                  re.IGNORECASE | re.MULTILINE)
    return m.group(1).strip() if m else None

def _find_pct(text, key):
    # like _find but for %-prefixed compile keys (e.g. %compilewater).
    m = re.search(r'"?%%%s"?\s+"?([^"\r\n]+?)"?\s*(?://[^\r\n]*)?\s*$' % key, text,
                  re.IGNORECASE | re.MULTILINE)
    return m.group(1).strip() if m else None

def _find_include(text):
    m = re.search(r'"?include"?\s+"([^"]+)"', text, re.IGNORECASE)
    return m.group(1).strip() if m else None

def _shader_name(text):
    m = re.match(r'\s*(?://[^\r\n]*\r?\n\s*)*"?([A-Za-z_][A-Za-z_0-9]*)"?', text)
    return m.group(1).lower() if m else None

def _vec3(s):
    """Parse a Source color/vector - '{.4 .4 .2}', '[1 1 1]', '.5 .5 .5' -> [r,g,b].
    Curly braces conventionally mean 0-255 ints; VtMB water VMTs mix both, so any
    component > 1 flags the whole triple as 0-255 and it's divided down."""
    if not s:
        return None
    nums = re.findall(r'-?[\d.]+', s)
    if len(nums) < 3:
        return None
    v = [float(x) for x in nums[:3]]
    return [c / 255.0 for c in v] if any(c > 1.0 for c in v) else v

def _find_f(text, key):
    s = _find(text, key)
    try:
        return float(s) if s is not None else None
    except ValueError:
        return None

def parse(text, resolve_include=None):
    """text: VMT contents. resolve_include: fn(path)->text for patch shaders.
    Returns dict with basetexture (normalized), selfillum, translucent, alphatest."""
    text = _decomment(text)
    if re.match(r'\s*"?patch"?', text, re.IGNORECASE) and resolve_include:
        inc = _find_include(text)
        if inc:
            base = resolve_include(inc)
            if base:
                # patch may override params; merge child over parent
                parent = parse(base, resolve_include)
                bt = _find(text, "basetexture")
                if bt:
                    parent["basetexture"] = bt.replace("\\", "/").lower()
                return parent

    bt = _find(text, "basetexture")
    nm = _find(text, "normalmap")
    is_water = _shader_name(text) == "water" or _find_pct(text, "compilewater") is not None
    env = _find(text, "envmap")
    envmask = _find(text, "envmapmask")
    bt2 = _find(text, "basetexture2")
    bump = _find(text, "bumpmap")
    return {
        "basetexture": bt.replace("\\", "/").lower() if bt else None,
        "selfillum": _find(text, "selfillum") == "1",
        "translucent": _find(text, "translucent") == "1",
        "alphatest": _find(text, "alphatest") == "1",
        # $additive: additive blend (SHADER_BLEND_ONE,ONE) - the base texture RGB is
        # added onto the framebuffer, full-bright on UnlitGeneric. Used for glow
        # overlays (e.g. the "on" glass pane of light-fixture models).
        "additive": _find(text, "additive") == "1",
        # decals ($decal 1): the shader name distinguishes an unlit decal (full
        # bright) from a lightmapped one (takes the underlying face's baked light);
        # $decalscale sizes the quad = texture pixel dims x scale (engine.dll
        # R_DecalSize: GetMappingWidth/Height * $decalScale). Default 1.0.
        "shader": _shader_name(text),
        "decal": _find(text, "decal") == "1",
        "decalscale": _find_f(text, "decalscale") or 1.0,
        # $envmap: cubemap reflection (38% of VtMB world materials). The base VMT's
        # value is the placeholder "env_cubemap"; VBSP patches each instance to the
        # nearest baked cubemap (maps/<map>/c<x>_<y>_<z> or cubemapdefault) - see
        # bsp_to_scene. The DX8 LightmappedGeneric_*EnvMap* path is flat-masked and
        # additive (confirmed against stdshader_dx8.dll: no Fresnel on the world
        # path), reflection = cube(reflect(v,n)) * mask * tint, with optional
        # contrast (mix toward squared) and saturation (mix from grayscale).
        "envmap": env.replace("\\", "/").lower() if env else None,
        "envmapmask": envmask.replace("\\", "/").lower() if envmask else None,
        "basealphaenvmapmask": _find(text, "basealphaenvmapmask") == "1",
        "envmaptint": _vec3(_find(text, "envmaptint")),        # [r,g,b] or None (=1,1,1)
        "envmapcontrast": _find_f(text, "envmapcontrast"),     # default 0 (no-op)
        "envmapsaturation": _find_f(text, "envmapsaturation"), # default 1 (no-op)
        # WorldVertexTransition (terrain 2-texture blend keyed by vertex/disp alpha).
        "basetexture2": bt2.replace("\\", "/").lower() if bt2 else None,
        # $bumpmap: tangent-space normal map (2% of world materials).
        "bumpmap": bump.replace("\\", "/").lower() if bump else None,
        # water (the "Water" shader): no $basetexture; a normal map + fog params.
        "water": is_water,
        "normalmap": nm.replace("\\", "/").lower() if nm else None,
        "fogcolor": _vec3(_find(text, "fogcolor")),      # [r,g,b] 0-1, or None
        "fogstart": _find_f(text, "fogstart"),           # Source units (inches)
        "fogend": _find_f(text, "fogend"),
        "reflecttint": _vec3(_find(text, "reflecttint")),
    }
