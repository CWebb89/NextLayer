import json, sys
config, names = sys.argv[1], sys.argv[2]
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
missing = engine - shown - skip
print(len(engine), len(shown & engine), len(missing), ",".join(sorted(missing)))
