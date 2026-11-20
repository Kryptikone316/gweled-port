import sys
from PIL import Image
for p in sys.argv[1:]:
    Image.open(p).convert('RGB').save(p.rsplit('.',1)[0]+'.png')
