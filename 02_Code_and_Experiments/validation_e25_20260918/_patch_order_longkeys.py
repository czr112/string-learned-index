import io
p = r'02_Code_and_Experiments/validation_e25_20260918/check_depth_dir_order.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
old = """    out.emplace_back(std::string(), v++);
    out.emplace_back(std::string(1, '\\0'), v++);
    out.emplace_back(std::string("\\0\\0", 2), v++);
    for (unsigned b = 0x70; b <= 0x90; ++b) add(std::string("pfx") + byte_str(b));
    return out;
}
"""
new = """    out.emplace_back(std::string(), v++);
    out.emplace_back(std::string(1, '\\0'), v++);
    out.emplace_back(std::string("\\0\\0", 2), v++);
    for (unsigned b = 0x70; b <= 0x90; ++b) add(std::string("pfx") + byte_str(b));
    // Long keys, including one that ends on a high byte: they do not fit a sub-block, so they
    // exercise the oversized placement, the scan materialisation and the erase path with the
    // directory mapping.
    add(std::string(4095, 'L') + "M");
    add(std::string(4095, 'L') + byte_str(0xff));
    return out;
}
"""
if s.count(old) != 1:
    raise SystemExit('anchor not found: %d' % s.count(old))
io.open(p, 'w', encoding='utf-8', newline='\n').write(s.replace(old, new))
print('long keys added to the index fixture')
