"""Generate the utility's two small shield icons; Pillow is a build-time tool only."""
from pathlib import Path
from PIL import Image, ImageDraw

root = Path(__file__).parent / 'src'
for name, accent in [('off', '#8c9bad'), ('on', '#63e2b1')]:
    image = Image.new('RGBA', (256, 256))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((12, 12, 244, 244), radius=55, fill='#10151f')
    draw.polygon([(128, 40), (203, 69), (194, 152), (170, 185), (128, 213),
                  (86, 185), (62, 152), (53, 69)], fill=accent)
    draw.line([(90, 126), (116, 153), (165, 101)], fill='#10151f', width=17, joint='curve')
    image.save(root / f'guard-{name}.ico', sizes=[(16, 16), (24, 24), (32, 32),
                                                (48, 48), (64, 64), (128, 128), (256, 256)])
