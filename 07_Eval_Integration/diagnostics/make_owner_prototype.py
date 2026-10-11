"""Generate the M3 prototype: LITS regions skip the reference table.

The probe is a copy of the pinned index.hpp plus bounded, anchor-checked edits:

  1. Config gains `lits_skip_reference_table` (default false, probe-only).
  2. The reference-table training moves DOWN to the LITS calibration site -- the first point
     where the mapping is decided -- and runs only when the region will use the table.
     Nothing between the two positions reads `model_`, and the prototype runner additionally
     cross-checks the layout numbers against the production-header entry device, because
     "the answers are still right" does not catch a mapping that quietly became degenerate.
  3. A preset offered for a LITS-mapped region with the table skipped is REFUSED loudly.
  4. `group_of()` refuses on a region with no resident table instead of reading an empty one.

Mapping, calibration, rejection domain, fallback coverage, query, update and scan paths are
the pinned source. Every replacement asserts its anchor is unique. Production is untouched.
"""
import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT.parent / "02_Code_and_Experiments/validation_e25_20260918"
PIN_INDEX = "640660a32f1fd1476d0d889d541c007883e5597d70e476524e3bcf2556ec8aa1"
PIN_SWITCHES = "5486909fffc61c1eef940c058eae3fe036e970f0db21c988b3b41800803f0d4e"

CONFIG_ANCHOR = "    bool offset_directory = true;\n"
CONFIG_FIELD = (
    "    // M3 prototype only: when set, a region whose mapping is the LITS backend's does\n"
    "    // not train and does not keep the Context16 reference table. Ignored for every\n"
    "    // other backend and for regions that fall back to the reference model.\n"
    "    bool lits_skip_reference_table = false;\n")

TRAIN_ANCHOR = (
    "        {\n"
    "            const auto t0 = E14_T0();\n"
    "            model_.train(sorted, prefix_.size(), config_.model_kind);\n"
    "            E14_ADD(model, t0);\n"
    "        }\n")
TRAIN_NEW = (
    "        // (M3 prototype: the reference table is trained below, once the mapping is known.)\n")

CALIBRATE_ANCHOR = (
    "        if (config_.model_backend == ModelBackend::LitsHpt) {\n"
    "            calibrate_lits(sorted, lits_shared_);\n"
    "        }\n"
    "        // E31H: hysteresis state, allocated only when the switch asks for it.\n")
CALIBRATE_NEW = (
    "        if (config_.model_backend == ModelBackend::LitsHpt) {\n"
    "            calibrate_lits(sorted, lits_shared_);\n"
    "        }\n"
    "        // M3 prototype: train the reference table only when this region will use it.\n"
    "        if (!(config_.lits_skip_reference_table && use_lits_)) {\n"
    "            const auto t0 = E14_T0();\n"
    "            model_.train(sorted, prefix_.size(), config_.model_kind);\n"
    "            E14_ADD(model, t0);\n"
    "        }\n"
    "        // E31H: hysteresis state, allocated only when the switch asks for it.\n")

PRESET_ANCHOR = "        const bool preset_ok = preset != nullptr && preset->group_ids.size() == sorted.size() &&\n"
PRESET_NEW = (
    "        if (preset != nullptr && config_.lits_skip_reference_table && use_lits_)\n"
    "            throw std::logic_error(\n"
    "                \"M3 prototype: a preset was offered for a LITS region whose reference \"\n"
    "                \"table is not resident; refused rather than silently degraded\");\n"
    + PRESET_ANCHOR)

GROUP_OF_ANCHOR = (
    "    std::uint32_t group_of(std::string_view suffix) const {\n"
    "        E18_INC(group_calls);\n")
GROUP_OF_NEW = (
    GROUP_OF_ANCHOR
    + "        if (model_.cumulative.empty())\n"
      "            throw std::logic_error(\n"
      "                \"M3 prototype: this region has no resident reference table; a caller \"\n"
      "                \"that needs the reference model must use an independent object\");\n")

RESIDENT_ANCHOR = "    bool uses_lits() const { return use_lits_; }\n"
RESIDENT_NEW = (
    RESIDENT_ANCHOR
    + "    bool reference_table_resident() const { return !model_.cumulative.empty(); }\n")


def _replace(source, old, new, what):
    if source.count(old) != 1:
        raise RuntimeError("prototype anchor %r matched %d times" % (what, source.count(old)))
    return source.replace(old, new)


def generate(destination):
    raw = (UPSTREAM / "index.hpp").read_bytes()
    if hashlib.sha256(raw).hexdigest() != PIN_INDEX:
        raise RuntimeError("prototype source drift")
    if hashlib.sha256((UPSTREAM / "switches.hpp").read_bytes()).hexdigest() != PIN_SWITCHES:
        raise RuntimeError("switches drift")
    source = raw.decode()
    source = _replace(source, CONFIG_ANCHOR, CONFIG_ANCHOR + CONFIG_FIELD, "Config field")
    source = _replace(source, TRAIN_ANCHOR, TRAIN_NEW, "training call moved")
    source = _replace(source, CALIBRATE_ANCHOR, CALIBRATE_NEW, "conditional training")
    source = _replace(source, PRESET_ANCHOR, PRESET_NEW, "preset refusal")
    source = _replace(source, GROUP_OF_ANCHOR, GROUP_OF_NEW, "group_of guard")
    source = _replace(source, RESIDENT_ANCHOR, RESIDENT_NEW, "resident accessor")
    if source.count("calibrate_lits(sorted, lits_shared_);") != 1:
        raise RuntimeError("calibration call not unique after the edits")
    if source.count("model_.train(sorted, prefix_.size(), config_.model_kind);") != 1:
        raise RuntimeError("training call not unique after the edits")
    destination.mkdir(exist_ok=False)
    path = destination / "index.hpp"
    path.write_text(source)
    return path


if __name__ == "__main__":
    p = generate(Path(sys.argv[1]))
    print(p, hashlib.sha256(p.read_bytes()).hexdigest())