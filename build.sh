zcc +zx -vn -startup=31 -DWFRAMES=3 -clib=sdcc_iy -SO3 --max-allocs-per-node20000 --fsigned-char @zproject.lst -o ufo_zx -pragma-include:zpragma.inc

z88dk.z88dk-appmake +zx -b ufo_zx_CODE.bin -o game.tap --blockname game --org 25124 --noloader

cat loader.tap screen.tap  game.tap > ufo_zx.tap
#cat loader.tap game.tap > ufo_zx.tap

# optional
# zxtap2wav-1.0.3-linux-amd64 -a -i ufo_zx.tap ufo_zx.wav
