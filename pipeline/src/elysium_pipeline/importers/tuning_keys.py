"""One-time authored tuning key migration; values stay untouched."""


def model_key_plan(keys, cast):
    result, owners = {}, {}
    for original in keys:
        key = str(original).strip().replace("\\", "/").lower()
        id = key if key in cast["models"] else cast["aliases"].get(key)
        if id not in cast["models"]:
            candidates = cast.get("ambiguousAliases", {}).get(key, [])
            raise ValueError(f"tuning key {original!s} has no unique cast model: {candidates}")
        if id in owners:
            raise ValueError(f"tuning keys {owners[id]!s} and {original!s} both name {id}")
        result[str(original)] = id
        owners[id] = original
    return result
