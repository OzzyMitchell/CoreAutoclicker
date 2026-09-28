"""Run with fontTools installed; input is Google Fonts' Lexend[wght].ttf."""
import sys
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont
from fontTools import subset

font = instantiateVariableFont(TTFont(sys.argv[1]), {"wght": 400}, inplace=True)
options = subset.Options()
options.name_IDs = [0, 1, 2, 3, 4, 5, 6]
options.hinting = False
options.layout_features = ["kern"]
options.name_legacy = True
options.name_languages = [0x409]
subsetter = subset.Subsetter(options=options)
subsetter.populate(unicodes=range(0x20, 0x7f))
subsetter.subset(font)
for record in font["name"].names:
    names = {1: "Core Lexend", 2: "Regular", 3: "Core Lexend Regular 1.0",
             4: "Core Lexend Regular", 6: "CoreLexend-Regular"}
    if record.nameID in names:
        record.string = names[record.nameID].encode(record.getEncoding())
font.save(sys.argv[2])
