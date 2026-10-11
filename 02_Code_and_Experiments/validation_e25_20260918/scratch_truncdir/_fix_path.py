import io
p = "RESULTS_DEPTH_DIR_TRUNC_20261011.md"
t = io.open(p, encoding="utf-8", newline="").read()
old = "`.tmp_truncdir_verify/verify_truncdir.py`"
new = "`../02_Code_and_Experiments/validation_e25_20260918/scratch_truncdir/independent_python/verify_truncdir.py`"
assert t.count(old) == 1, t.count(old)
io.open(p, "w", encoding="utf-8", newline="").write(t.replace(old, new))
print("doc path updated")
