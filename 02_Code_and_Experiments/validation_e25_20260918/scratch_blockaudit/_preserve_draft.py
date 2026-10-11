import hashlib, os, shutil
D = r'D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918'
GATE = os.path.join(D, 'check_block_rank.cpp')
DRAFT = os.path.join(D, 'scratch_blockaudit', 'agent_draft_check_block_model_backend.cpp')

shutil.copy2(GATE, DRAFT)
print('agent draft preserved ->', DRAFT, os.path.getsize(DRAFT))

base = open(os.path.join(D, 'scratch_blockaudit', '_base_gate.cpp'), 'w', encoding='utf-8', newline='')
base.write('')  # placeholder, real content follows
base.close()
print('placeholder written')
