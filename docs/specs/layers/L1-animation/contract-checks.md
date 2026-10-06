# Contract checks — engine-replaced rows that bear state or event order (L1-animation)

`decisions.md` D3: Unreal does these jobs instead of the Source mechanism. The modernization rule admits
that only with the retail contract kept — the same inputs, outputs and event order. Each row is checked
before the layer closes (gate item 5): a record shows the contract holds, or the row becomes a named
divergence the owner accepts. 14 other engine-replaced core rows of this layer are
presentation, networking, memory or debug and need no check.

| subsystem | address | function | maps | what it does | how the port replaces it |
|---|---|---|---|---|---|
| animation | `0x1008eb70` | CBaseAnimating::SlerpBones | 108 | Resolve a sequence across the base and two extra models, then blend its bones using the selected model's bone mapping. | Visual pose evaluation and bone blending use baked Unreal clips and AnimGraph nodes. Source studio-header routing is replaced by clip ownership; the port explic |
| animation | `0x100c1ea0` | Global::FUN_100c1ea0 | 108 | Copies each studio bone’s local position and quaternion into the caller’s pose arrays. | Unreal’s skeletal animation evaluation supplies the local pose instead of copying these Source studio-header rows. |
| animation | `0x100c81b0` | Global::FUN_100c81b0 | 108 | Builds a bone bit mask by remapping selected bones through the model’s bone-map rows. | Per-bone pose filtering is part of Unreal’s skeletal animation and bone-container path; the Source mask remap is not run. |
| animation | `0x100c17e0` | Global::FUN_100c17e0 | 108 | Resolves a sequence descriptor locally or through included models while initializing the bone index mapping for pose evaluation. | Unreal’s skeletal reference skeleton and animation graph handle sequence-to-bone evaluation instead of this Source descriptor and mapping path. |
| animation | `0x100c1870` | Global::FUN_100c1870 | 108 | Finds the animation group and frame for a sequence index, remaps controller indices, and returns pose data. | This is Source’s compressed animation-group lookup and decoding path. The port resolves baked UAnimSequence assets and has no runtime decoder. |
| animation | `0x100c1f40` | Global::FUN_100c1f40 | 108 | Normalize sequence cycle, resolve two pose-parameter axes, decode neighboring animation poses and blend their bone positions/quaternions. | Baked UAnimSequence/UBlendSpace players own skeletal pose evaluation. The port forwards pose axes and looping instead of recreating studio decoding and BlendBon |
| animation | `0x100c2420` | Global::FUN_100c2420 | 108 | Samples selected bones’ compressed rotation and position channels at a fractional frame. | UE AnimSequence evaluation replaces Source’s runtime frame sampler; the port uses baked animation assets rather than decoding these channels at runtime. |
| animation | `0x100c25c0` | Global::FUN_100c25c0 | 108 | Evaluates a sequence’s bone pose and recursively applies included-model sequences, blend layers, and pose parameters. | The Source studio bone evaluator is replaced by Unreal’s animation graph and skeletal pose evaluation. |
| animation | `0x100c4640` | Global::FUN_100c4640 | 108 | Initializes global fixed-size bone setup cache and free-list tables once. | Source’s global animation cache allocator is replaced by Unreal’s object and container lifetime management. |
| animation | `0x1013b0d0` | Global::FUN_1013b0d0 | 108 | Convert an XYZW quaternion into the nine rotation entries of a 3×4 matrix, leaving translation untouched. | Unreal supplies skeletal rotation matrices and FTransform matrix conversion; the Source matrix-building helper is replaced by engine transform math. |
