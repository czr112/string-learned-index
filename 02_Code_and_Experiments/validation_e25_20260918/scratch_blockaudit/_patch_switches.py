import re, hashlib
SW = r'D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\switches.hpp'
src = open(SW, 'r', encoding='utf-8', newline='').read()
assert '\r\n' not in src

def sub(t, a, r, label):
    n = t.count(a)
    if n != 1:
        raise SystemExit('ANCHOR FAIL %s: %d\n%s' % (label, n, a[:300]))
    return t.replace(a, r)

# 1) printer
a = '''           ",model_backend=" + (c.model_backend == validation::ModelBackend::Context16
                                ? "Context16" : "LitsHpt");'''
r = '''           ",model_backend=" + backend_name(c.model_backend);'''
src = sub(src, a, r, 'printer')

# 2) add backend_name helper before switches()
a = '''// The effective switch values, for the gate's own output. A reader should never have
// to infer which configuration produced a line.
inline std::string switches(const validation::Config& c) {'''
r = '''// The backend name, for gate output. A three-valued field printed as a two-valued one turns
// BlockLinearRank into "LitsHpt", and a reader comparing two arms' printed lines would then
// conclude the arms differ in nothing. The switch to the third backend is what made this a
// real defect rather than a latent one.
inline const char* backend_name(validation::ModelBackend b) {
    switch (b) {
        case validation::ModelBackend::Context16: return "Context16";
        case validation::ModelBackend::LitsHpt: return "LitsHpt";
        case validation::ModelBackend::BlockLinearRank: return "BlockLinearRank";
    }
    return "?";
}

// The effective switch values, for the gate's own output. A reader should never have
// to infer which configuration produced a line.
inline std::string switches(const validation::Config& c) {'''
src = sub(src, a, r, 'backend_name')

# 3) field table: rewrite as a DIFFERS table
a = '''    struct Field { const char* name; bool x; bool y; };
    const Field fields[] = {'''
r = '''    // THE TABLE HOLDS "DIFFERS", NOT A BOOL ARM VALUE. The previous shape stored each
    // configuration's value and compared them, which works only for a two-valued switch: with
    // `model_backend` a three-valued enum, encoding it as "== LitsHpt" made Context16 vs
    // BlockLinearRank look like NO difference, so a gate naming that field would have thrown
    // "should not differ" while the arms did differ. A table that cannot express a difference
    // cannot check one.
    struct Field { const char* name; bool differs; };
    const Field fields[] = {'''
src = sub(src, a, r, 'field struct')

# transform each row: {"name", a.X, b.X}, -> {"name", a.X != b.X},
rows = re.findall(r'\{"([a-z_]+)", a\.([a-z_]+), b\.([a-z_]+)\}', src)
if len(rows) != 23:
    raise SystemExit('unexpected row count %d' % len(rows))
for name, fa, fb in rows:
    assert fa == fb, (name, fa, fb)
    old = '{"%s", a.%s, b.%s}' % (name, fa, fb)
    new = '{"%s", a.%s != b.%s}' % (name, fa, fb)
    src = sub(src, old, new, 'row ' + name)

# the model_backend row was multi-line; handle it explicitly
a = '''        {"model_backend", a.model_backend == validation::ModelBackend::LitsHpt,
                          b.model_backend == validation::ModelBackend::LitsHpt},'''
r = '''        // Three-valued: the row now tests the enum itself, so Context16 vs BlockLinearRank IS
        // a difference and an arm pair differing only in the backend is checked rather than
        // waved through.
        {"model_backend", a.model_backend != b.model_backend},'''
src = sub(src, a, r, 'row model_backend')

# 4) the loop
a = '''        if ((f.x != f.y) != expected) {'''
r = '''        if (f.differs != expected) {'''
src = sub(src, a, r, 'loop')

open(SW, 'w', encoding='utf-8', newline='').write(src)
print('switches.hpp patched, sha256', hashlib.sha256(open(SW,'rb').read()).hexdigest())

