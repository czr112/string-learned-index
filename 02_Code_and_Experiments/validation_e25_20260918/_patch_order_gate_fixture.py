# -*- coding: utf-8 -*-
"""Fix the order gate's index fixture: bulk_load takes keys ALREADY in the index's order."""
import io, os
HERE = os.path.dirname(os.path.abspath(__file__))
p = os.path.join(HERE, "check_depth_dir_order.cpp")
t = io.open(p, encoding="utf-8", newline="").read()

def sub(old, new, what):
    global t
    if t.count(old) != 1:
        raise SystemExit("FATAL %s: found %d" % (what, t.count(old)))
    t = t.replace(old, new)

sub("""    RefIndex ref;
    auto idx = std::make_unique<Index>(c);
    {
        // The input exists only inside this scope: after it ends, nothing in the index may
        // still point into it (the lifetime rule the LITS round had to fix, checked here for the
        // directory's arena and the demand preset).
        std::vector<Pair> input = byte_domain_keys();
        for (const auto& kv : input) ref.rows.push_back(kv);
        ref.sort_it();
        idx->bulk_load(input);
    }
""",
"""    RefIndex ref;
    auto idx = std::make_unique<Index>(c);
    {
        // CALLING CONVENTION, asserted rather than assumed: bulk_load takes the keys ALREADY in
        // the index's own order, which is unsigned-byte order. `std::string::operator<` compares
        // char, and char is signed here, so it is NOT a valid sort for keys with bytes >= 0x80;
        // the ASCII corpora hide that, which is why the fixture is sorted with the index's own
        // compare() and the result is checked below.
        std::vector<Pair> input = byte_domain_keys();
        std::sort(input.begin(), input.end(),
                  [](const Pair& a, const Pair& b) { return compare(a.first, b.first) < 0; });
        bool ascending = true;
        for (std::size_t i = 1; i < input.size(); ++i)
            if (!(compare(input[i - 1].first, input[i].first) < 0)) ascending = false;
        require(ascending, tag + ": the fixture is ascending in the index's unsigned order");
        require(input.front().first.empty() && !input.front().first.empty() + 1,
                tag + ": the fixture starts at the empty key");
        for (const auto& kv : input) ref.rows.push_back(kv);
        ref.sort_it();
        // The input itself exists only inside this scope: after it ends, nothing in the index may
        // still point into it (the lifetime rule the LITS round had to fix, checked here for the
        // directory's arena and the demand preset).
        idx->bulk_load(input);
    }
""", "fixture sort")

# drop the silly tautological require and the leftover (void)full
sub("""        require(input.front().first.empty() && !input.front().first.empty() + 1,
                tag + ": the fixture starts at the empty key");
""", "", "tautology")
sub("""    (void)full;
""", "", "void full")
io.open(p, "w", encoding="utf-8", newline="\n").write(t)
print("patched", p)
