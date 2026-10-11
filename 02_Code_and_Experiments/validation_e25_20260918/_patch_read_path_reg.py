import io, os
p = r"D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\tools_build.py"
s = io.open(p, encoding="utf-8", newline="").read()

a = '    "san_check_depth_dir_trunc": ["-DE12_COUNT"],\n'
b = a + ('    # P1 read-path entry diagnosis (2026-10-11): read-side counters only, no timing; the\n'
         '    # device asserts its own mode and reconciliation, so it takes both diagnostic macros.\n'
         '    "diag_depth_dir_read_path": ["-DE25_DIAG", "-DE12_COUNT"],\n')
assert s.count(a) == 1, "flags anchor"
s = s.replace(a, b)

a = '       "san_check_depth_dir_trunc": "check_depth_dir_trunc.cpp",\n'
b = a + ('       # P1 read-path entry diagnosis: one source, no sanitizer twin yet (counts-only; the\n'
         '       # device fails closed on its own assertions).\n'
         '       "diag_depth_dir_read_path": "diag_depth_dir_read_path.cpp",\n')
assert s.count(a) == 1, "src anchor"
s = s.replace(a, b)
io.open(p, "w", encoding="utf-8", newline="\n").write(s)
print("tools_build.py patched")
