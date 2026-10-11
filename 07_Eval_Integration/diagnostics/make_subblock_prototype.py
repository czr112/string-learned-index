"""Generate an immutable Q prototype from one exact source; never edits production."""
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT.parent / "02_Code_and_Experiments/validation_e25_20260918"
PIN = "640660a32f1fd1476d0d889d541c007883e5597d70e476524e3bcf2556ec8aa1"

def generate(destination):
    raw = (UPSTREAM / "index.hpp").read_bytes()
    if hashlib.sha256(raw).hexdigest() != PIN:
        raise RuntimeError("prototype source drift")
    source = raw.decode()
    def replace(old, new):
        nonlocal source
        if source.count(old) != 1:
            raise RuntimeError("prototype anchor not unique: " + old[:80])
        source = source.replace(old, new)
    replace("    bool locate = false;\n};", "    bool locate = false;\n    bool q_static = false; // Q prototype only; no mutable use permitted.\n};")
    replace("    std::vector<std::vector<Boundary>> dirs_;", """    std::vector<std::vector<Boundary>> dirs_;
    // Q static prototype: built after bulk_load, never refreshed after mutations.
    mutable std::vector<std::uint64_t> q_hot_;
    mutable std::vector<std::uint32_t> q_start_;
""")
    replace("    // --- E16: derivations shared by the constructor and the statistics-only path", """    static std::uint64_t q_summary(std::string_view key) {
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < 8; ++i)
            value = (value << 8) | (i < key.size() ? static_cast<unsigned char>(key[i]) : 0);
        return value;
    }
    std::size_t locate_subblock_q(std::uint32_t g, std::string_view s, Result& r) const {
        if (q_start_.size() != std::size_t(groups_) + 1)
            throw std::logic_error("Q static directory was not prepared");
        const auto& dir = dirs_[g];
        const std::size_t begin = q_start_[g];
        if (q_start_[g + 1] - begin != dir.size())
            throw std::logic_error("Q directory changed after preparation");
        const auto query = q_summary(s);
        std::size_t lo = 0, hi = dir.size();
        while (lo < hi) {
            const auto mid = lo + (hi - lo) / 2;
            E12_TICK(r.locate_probes);
            const auto fence = q_hot_[begin + mid];
            const bool before = query == fence ? compare(s, dir[mid].key) < 0 : query < fence;
            if (before) hi = mid; else lo = mid + 1;
        }
        return lo == 0 ? NO_SUBBLOCK : dir[lo - 1].subblock;
    }

    // --- E16: derivations shared by the constructor and the statistics-only path""")
    replace("            const std::size_t cand = locate_subblock(g, s, result);", "            const std::size_t cand = mode.q_static ? locate_subblock_q(g, s, result)\n                                                    : locate_subblock(g, s, result);")
    replace("    std::size_t directory_rebuilds() const { return dir_rebuilds; }", """    // Static experiment only. Caller promises no writes after this point.
    void prepare_q_static() const {
        if (!q_start_.empty()) throw std::logic_error("Q already prepared");
        std::size_t total = 0;
        for (const auto& dir : dirs_) total += dir.size();
        if (total > UINT32_MAX) throw std::length_error("Q directory too large");
        q_hot_.reserve(total);
        q_start_.reserve(std::size_t(groups_) + 1);
        for (std::uint32_t g = 0; g < groups_; ++g) {
            q_start_.push_back(static_cast<std::uint32_t>(q_hot_.size()));
            if (!dirs_.empty())
                for (const auto& boundary : dirs_[g]) q_hot_.push_back(q_summary(boundary.key));
        }
        q_start_.push_back(static_cast<std::uint32_t>(q_hot_.size()));
    }
    std::size_t q_extra_bytes() const {
        return sizeof(q_hot_) + sizeof(q_start_) + q_hot_.capacity() * sizeof(std::uint64_t) +
               q_start_.capacity() * sizeof(std::uint32_t);
    }
    std::size_t directory_rebuilds() const { return dir_rebuilds; }""")
    destination.mkdir(exist_ok=False)
    path = destination / "index.hpp"
    path.write_text(source)
    return path

if __name__ == "__main__":
    import sys
    generate(Path(sys.argv[1]))
