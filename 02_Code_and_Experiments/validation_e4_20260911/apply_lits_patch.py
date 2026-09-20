"""Apply the recorded PMSS interpolation-bound fix to the pinned source."""
from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parent
path=root/'vendor/lits/LITS/lits/lits_pmss.hpp'
text=path.read_text()
changes=[('int das_upper_bound = int(das) + 1;','int das_upper_bound = std::min(int(das) + 1, das_delt - 1);'),('int pkl_upper_bound = int(pkl) + 1;','int pkl_upper_bound = std::min(int(pkl) + 1, pkl_delt - 1);')]
for old,new in changes:
    if old in text:
        if text.count(old)!=1:raise RuntimeError('unexpected patch target count')
        text=text.replace(old,new)
    elif new not in text:raise RuntimeError('patch target missing')
path.write_text(text)
print('Applied PMSS table-bound patch',hashlib.sha256(path.read_bytes()).hexdigest())
path=root/'vendor/lits/LITS/lits/lits_model.hpp';text=path.read_text()
for loop in ['for (int i = gcpl; key[i] && ps >= 1; ++i) {','for (int i = gcpl; key[i] && pro >= min_double; ++i) {']:
    old=loop+'\n            const auto &uni = m[i & PS_MASK][key[i - 1] & FC_MASK][key[i]];'
    new=loop+'\n            const auto &uni = m[i & PS_MASK][i == 0 ? 0 : (key[i - 1] & FC_MASK)][key[i]];'
    if old in text:text=text.replace(old,new)
    elif new not in text:raise RuntimeError('model patch target missing')
path.write_text(text)
print('Applied HPT initial-context patch',hashlib.sha256(path.read_bytes()).hexdigest())
