# Transforms in a 64 px icon world

Independent DreamSeeker/DreamDaemon **516.1688**, 2026-11-14, port 18628. No game assets or screenshots in Git.
`prepare.py <dir>` writes a world with `icon_size = 64`, a 576x384 skin (9x6 tiles) and solid icons: a 64 px icon with
a 32 px yellow box (white corner, translucent step) and a 32 px icon with a 16 px blue box. `tools/stockprobe/stockprobe.py pair`
grabs stock and Fugue at 1:1; `stock.json` keeps what stock drew and Fugue's difference per 192 px
case crop. `entitymap_test::wideIconTransformTreesLikeStock` consumes it.

## Independent conclusions

A 64 px world follows the 32 px rules: a turned or scaled atom's overlay takes the matrix about its own pivot
(`turn-overlay`, `scale-overlay`), an image on a turned target turns about its own centre (`image-turn`), an image's own
matrix applies (`image-own-turn`), and maptext turns about the icon's centre (`turn-maptext`).

## Fugue after the change

The 32 px world checks on transformed ordinary trees and on images of transformed targets are gone. Every case matches
stock exactly except `turn-maptext`, whose 74 differing pixels are glyph rasterization (as in 32 px worlds).

## Not covered

Non-square icon sizes, icons larger than the world's icon size in a non-32 px world, and HUD master trees (their 32 px
check is part of a separate structural contract).
