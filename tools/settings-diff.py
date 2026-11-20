import json, sys
config, names = sys.argv[1], sys.argv[2]
capabilities = sys.argv[3] if len(sys.argv) > 3 else None
engine = set()
for section in ("print_settings", "filament_settings", "printer_settings"):
    engine |= set(json.load(open(config))["configuration"][section])
shown = set(open(names).read().split())
# Deliberately absent: single-nozzle colour mixing (this app drives tool
# changers) and `bed_model`, which is the bed plate's mesh path rather than a
# print setting -- the viewport loads that mesh itself.
skip = {name for name in engine
        if name.startswith(("filament_flush", "mmu_", "single_extruder_multi_material_"))}
skip.add("bed_model")
# The engine this application prints through cannot apply a setting it does not
# implement, so those names are absent from the sidebar on purpose: the shipped
# capability list is the record of which ones, and hiding one it does not name
# would still fail here.
if capabilities:
    skip |= set(json.load(open(capabilities))["unsupported"])
missing = engine - shown - skip
print(len(engine), len(shown & engine), len(missing), ",".join(sorted(missing)))
