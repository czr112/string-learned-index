import io
p = r"D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\tools_build.py"
s = io.open(p, encoding="utf-8", newline="").read()
a = '    "diag_depth_dir_read_path": ["-DE25_DIAG", "-DE12_COUNT"],\n'
b = a + ('    # M4 entry diagnosis (2026-10-11): the merge shadow and the scan counters are\n'
         '    # diagnostic-only; counts, no timing.\n'
         '    "diag_merge_entry": ["-DE25_DIAG"],\n')
assert s.count(a) == 1, "flags"
s = s.replace(a, b)
a = '       "diag_depth_dir_read_path": "diag_depth_dir_read_path.cpp",\n'
b = a + ('       # M4 entry diagnosis: one source, no sanitizer twin (counts-only device; it fails\n'
         '       # closed on its own assertions).\n'
         '       "diag_merge_entry": "diag_merge_entry.cpp",\n')
assert s.count(a) == 1, "src"
s = s.replace(a, b)
io.open(p, "w", encoding="utf-8", newline="\n").write(s)
print("tools_build.py: diag_merge_entry registered")
