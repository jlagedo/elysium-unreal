"""Decode one VMT into the complete material model plus its gapless byte ledger.

The decode is deliberately vocabulary-free: every key the source declares is carried in source
order with the bytes that declared it, rather than a fixed set of fields the renderer happened to
need. Shader and parameter *names* are data here, so a material using a key this pipeline has
never seen still publishes a complete unit.
"""

from __future__ import annotations

from typing import Any, Callable

from elysium_pipeline.formats.material_glb import lexer, shaders
from elysium_pipeline.formats.material_glb.coverage import ByteLedger
from elysium_pipeline.formats.material_glb.model import (
    MaterialModel,
    Parameter,
    ProxyRecord,
    surface_property_asset_id,
    texture_asset_id,
)


class MaterialDecodeError(RuntimeError):
    """The selected VMT cannot be read as a material."""


#: Parameters whose value names a texture. Measured over the install's 11,624 addressable VMTs:
#: each of these carries a value resolving to a `materials/**.tth` identity at least once. A value
#: that does not resolve stays a texture binding -- the binding is understood, the target is absent.
TEXTURE_PARAMETERS = frozenset({
    "$basetexture", "$basetexture2", "$texture2", "$bumpmap", "$normalmap", "$detail",
    "$detail2", "$envmap", "$envmapmask", "$glassenvmap", "$iris", "$glint", "$dudvmap",
    "$dudvtexture", "$spotlightmask", "$crackmaterial", "$masktexture", "$cloudalphatexture",
    "$burntexture", "$antitexture", "$fuzztexture", "%tooltexture", "$refracttexture",
    "$reflecttexture", "$selfillummask", "$lightwarptexture", "$bottommaterial",
})

#: The placeholder an authored `$envmap` carries where VBSP is expected to patch each face to the
#: nearest baked cubemap. It names no shipped texture, so it resolves as a symbol, not a binding.
#: 2,217 of the corpus's 2,610 `$envmap` values are exactly this string.
ENVIRONMENT_SYMBOL = "env_cubemap"

#: Source render targets the engine supplies at run time; they name no install member.
RENDER_TARGET_PREFIX = "_rt_"

#: The block that holds material proxies, whatever its source casing.
PROXY_BLOCK = "proxies"


def normalize_value_path(value: str) -> str:
    normalized = value.replace("\\", "/").strip().strip('"').lower()
    while "//" in normalized:
        normalized = normalized.replace("//", "/")
    return normalized.strip("/")


def _is_number(text: str) -> bool:
    try:
        float(text)
    except ValueError:
        return False
    return True


def _is_integer(text: str) -> bool:
    try:
        int(text, 10)
    except ValueError:
        return False
    return True


def value_type(value: str) -> str:
    """The parameter value's syntactic shape, independent of what its key means."""

    text = value.strip()
    if not text:
        return "empty"
    inner = text.strip("{}[]").strip()
    parts = inner.split()
    if len(parts) > 1 and all(_is_number(part) for part in parts):
        return "vector"
    if _is_integer(text):
        return "integer"
    if _is_number(text):
        return "number"
    return "string"


def _claim(ledger: ByteLedger, token: lexer.Token, owner: str) -> None:
    ledger.claim(token.offset, token.length, "mapped", owner)


def decode_material(
    closure,
    *,
    texture_exists: Callable[[str], bool] | None = None,
) -> MaterialModel:
    """The complete material unit for one source closure.

    `texture_exists` answers whether a texture identity is present in the install. Without it a
    binding publishes unresolved rather than asserting a target this decode never looked for.
    """

    member = closure.vmt
    text = lexer.decode_text(member.data)
    try:
        tokens = lexer.tokenize(text)
        document = lexer.parse(tokens)
    except lexer.MaterialLexError as error:
        raise MaterialDecodeError(f"{member.path}: {error}") from error

    ledger = ByteLedger(member.path, member.data)
    comments: list[dict[str, Any]] = []
    for token in tokens:
        if token.kind == "whitespace":
            ledger.claim(
                token.offset, token.length, "omitted-proven", "vmt.insignificant-whitespace"
            )
        elif token.kind == "comment":
            ledger.claim(token.offset, token.length, "mapped", f"vmt.comment[{len(comments)}]")
            comments.append({"offset": token.offset, "text": token.text})
        elif token.kind == "bom":
            ledger.claim(token.offset, token.length, "mapped", "vmt.byte-order-mark")

    _claim(ledger, document.shader, "vmt.shader")
    source_shader = document.shader.text
    shader = source_shader.strip().lower()

    parameters: list[Parameter] = []
    blocks: list[dict[str, Any]] = []

    def walk(block: lexer.Block, path: str) -> None:
        """Claim one block's own bytes, then every pair it declares, in source order."""

        if block.open_token is not None:
            _claim(ledger, block.open_token, f"vmt.block[{path}].open")
        if block.close_token is not None:
            _claim(ledger, block.close_token, f"vmt.block[{path}].close")
        for ordinal, pair in enumerate(block.pairs):
            name = pair.key.text.strip().lower()
            child = f"{path}/{name}#{ordinal}" if path else f"{name}#{ordinal}"
            if pair.block is not None:
                # An anonymous block's "key" is its own opening brace, which the block
                # itself claims; only a named block has a separate name token.
                if pair.key is not pair.block.open_token:
                    _claim(ledger, pair.key, f"vmt.block[{child}].name")
                blocks.append({
                    "name": name,
                    "sourceName": pair.key.text,
                    "path": child,
                    "parent": path,
                    "offset": pair.key.offset,
                })
                walk(pair.block, child)
                continue
            slot = len(parameters)
            _claim(ledger, pair.key, f"vmt.parameter[{slot}].key")
            if pair.value is not None:
                _claim(ledger, pair.value, f"vmt.parameter[{slot}].value")
            raw = "" if pair.value is None else pair.value.text
            parameters.append(
                Parameter(
                    index=slot,
                    block=path,
                    key=name,
                    source_key=pair.key.text,
                    value=raw,
                    value_type="none" if pair.value is None else value_type(raw),
                    quoted_key=pair.key.quoted,
                    quoted_value=bool(pair.value is not None and pair.value.quoted),
                    offset=pair.key.offset,
                )
            )

    walk(document.root, "")
    for ordinal, token in enumerate(document.trailing):
        _claim(ledger, token, f"vmt.content-after-shader-block[{ordinal}]")

    # Proxies are ordered records: one block may declare the same proxy more than once -- the
    # GlobalWetness triple is three blocks, one per `$envmaptint` channel -- and the order is the
    # order the engine applies them in.
    proxies: list[ProxyRecord] = []
    for entry in blocks:
        parent = entry["parent"]
        if not parent or parent.split("#")[0] != PROXY_BLOCK or "/" in parent:
            continue
        proxies.append(
            ProxyRecord(
                index=len(proxies),
                name=entry["name"],
                source_name=entry["sourceName"],
                parameters=tuple(
                    parameter for parameter in parameters if parameter.block == entry["path"]
                ),
            )
        )

    dependencies: list[dict[str, Any]] = []
    texture_bindings: list[dict[str, Any]] = []
    environment: dict[str, Any] | None = None
    surface_property: str | None = None
    unresolved: list[dict[str, Any]] = []

    for parameter in parameters:
        if parameter.block:
            continue                       # a proxy's operands are variables, not material inputs
        if parameter.key == "$surfaceprop" and parameter.value.strip():
            surface_property = parameter.value.strip().strip('"').lower()
            continue
        if parameter.key not in TEXTURE_PARAMETERS:
            continue
        raw = normalize_value_path(parameter.value)
        if not raw:
            continue
        if parameter.key == "$envmap" and raw == ENVIRONMENT_SYMBOL:
            environment = {"parameter": parameter.key, "symbol": raw}
            continue
        if raw.startswith(RENDER_TARGET_PREFIX):
            texture_bindings.append({
                "parameter": parameter.key,
                "value": raw,
                "kind": "render-target",
                "asset": None,
                "resolved": True,
            })
            continue
        asset = texture_asset_id(raw)
        present = bool(texture_exists(raw)) if texture_exists is not None else False
        texture_bindings.append({
            "parameter": parameter.key,
            "value": raw,
            "kind": "texture",
            "asset": asset,
            "resolved": present,
        })
        if present:
            dependencies.append({
                "role": "texture",
                "parameter": parameter.key,
                "asset": asset,
                "sourcePath": f"materials/{raw}.tth",
            })

    if surface_property:
        dependencies.append({
            "role": "surface-property",
            "asset": surface_property_asset_id(surface_property),
            "sourcePath": "scripts/surfaceproperties.txt#" + surface_property,
        })

    # `Patch` names another material as its base. No shipped VtMB material uses the shader -- the
    # convention postdates the game -- so this edge is represented but unexercised by the corpus.
    patch: dict[str, Any] | None = None
    if shader == "patch":
        include = next(
            (item for item in parameters if item.key == "include" and not item.block), None
        )
        if include is None:
            unresolved.append({"path": "patch.include", "reason": "patch-declares-no-include"})
        else:
            base = normalize_value_path(include.value)
            if base.startswith("materials/"):
                base = base[len("materials/"):]
            if base.endswith(".vmt"):
                base = base[:-len(".vmt")]
            patch = {
                "include": base,
                "asset": "vtmb:material:" + base,
                "operations": [
                    entry["name"]
                    for entry in blocks
                    if not entry["parent"] and entry["name"] in ("replace", "insert")
                ],
            }
            dependencies.append({
                "role": "material",
                "asset": patch["asset"],
                "sourcePath": f"materials/{base}.vmt",
            })

    # Which shipped program the parameters select. A family whose selector has not been
    # transcribed from the binary publishes unresolved rather than guessing from the name.
    bound = {
        binding["parameter"]
        for binding in texture_bindings
        if binding["kind"] == "texture" and binding["resolved"]
    }
    if environment is not None:
        # `env_cubemap` is a real bound cubemap at draw time; VBSP patches the face to a baked
        # cube, so the selector sees `$envmap` as a texture even though the VMT names a symbol.
        bound.add(environment["parameter"])
    resolution = shaders.resolve(
        shader, shaders.state_from_parameters(parameters, frozenset(bound))
    )
    shader_resolution = {
        "family": resolution.family,
        "resolved": resolution.resolved,
        "programs": [
            {
                "pixelShader": program.pixel_shader,
                "vertexShader": program.vertex_shader,
                "condition": program.condition,
                "drawPass": program.draw_pass,
            }
            for program in resolution.programs
        ],
        "inputs": list(resolution.inputs),
        "reason": resolution.reason,
    }

    omissions = [{
        "role": "keyvalues-insignificant-whitespace",
        "reason": "separator-bytes-carry-no-keyvalues-meaning",
    }]

    return MaterialModel(
        material_path=closure.material_path,
        asset_id=closure.asset_id,
        sources=[member.identity()],
        shader=shader,
        source_shader=source_shader,
        parameters=parameters,
        blocks=blocks,
        proxies=proxies,
        texture_bindings=texture_bindings,
        patch=patch,
        surface_property=surface_property,
        environment=environment,
        shader_resolution=shader_resolution,
        dependencies=dependencies,
        comments=comments,
        anomalies=list(document.anomalies),
        omissions=omissions,
        unresolved=unresolved,
        unsupported=[],
        byte_coverage=[ledger.finish()],
    )
