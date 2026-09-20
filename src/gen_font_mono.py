# JetBrains Mono (OFL) → src/font_mono.h, subset to what the UI can show (Latin, Latin-1, Cyrillic,
# general punctuation, arrows, math, box drawing / blocks) so the header stays small.
import io, os
from fontTools import subset
from fontTools.ttLib import TTFont

SP = os.path.dirname(os.path.abspath(__file__))
OUT = r"F:\GitHub\INU_Check-GTA\src\font_mono.h"
ofl = io.open(os.path.join(SP, 'OFL_JetBrainsMono.txt'), encoding='utf-8').read().strip().splitlines()

def subset_font(src, dst):
    opts = subset.Options()
    opts.layout_features = ['kern', 'liga', 'calt']
    opts.name_IDs = ['*']
    opts.notdef_outline = True
    opts.hinting = False
    f = TTFont(src)
    s = subset.Subsetter(opts)
    unicodes = list(range(0x20, 0x7F)) + list(range(0xA0, 0x100)) + list(range(0x400, 0x460)) + list(range(0x2000, 0x2070)) + list(range(0x2190, 0x2200)) + list(range(0x2200, 0x2300)) + list(range(0x2500, 0x25A0)) + [0x2116, 0x2122, 0x2713, 0x2714, 0x2717, 0x2318, 0x23CE]
    s.populate(unicodes=unicodes)
    s.subset(f)
    f.save(dst)
    return open(dst, 'rb').read()

reg = subset_font(os.path.join(SP, 'JetBrainsMono-Medium.ttf'), os.path.join(SP, 'jbm_regular_sub.ttf'))
bold = subset_font(os.path.join(SP, 'JetBrainsMono-Bold.ttf'), os.path.join(SP, 'jbm_bold_sub.ttf'))
print('regular', len(reg), 'bold', len(bold))

def emit(f, name, data):
    f.write('static const unsigned int %s_size = %d;\n' % (name, len(data)))
    f.write('static const unsigned char %s_data[%d] = {\n' % (name, len(data)))
    for i in range(0, len(data), 24):
        f.write('\t' + ','.join(str(b) for b in data[i:i + 24]) + ',\n')
    f.write('};\n')

with io.open(OUT, 'w', encoding='utf-8', newline='\n') as f:
    f.write('// JetBrains Mono (JetBrains) — SIL Open Font License 1.1, embedded as the gtacheck monospace face for the\n')
    f.write('// «Терминал» theme (Medium + Bold, subset to Latin / Latin-1 / Cyrillic / punctuation / arrows / math / box\n')
    f.write('// drawing). Generated from JetBrainsMono-2.304 by scratchpad gen_font_mono.py.\n//\n')
    for line in ofl: f.write('// ' + line + '\n' if line else '//\n')
    f.write('#ifndef GTACHECK_FONT_MONO_H\n#define GTACHECK_FONT_MONO_H\n')
    emit(f, 'mono_font', reg)
    emit(f, 'monob_font', bold)
    f.write('#endif\n')
print('written', OUT, os.path.getsize(OUT))
