#!/bin/sh
# pc/3DForge/tools/shots.sh -- the screenshots of 3DForge for Windows (screenshots/3dforge-win*.png), made by the
# program itself off the screen (3DForge.exe --shot, Shot.cs): the sample bracket, a box being made, Manufacture.
#   sh pc/3DForge/tools/shots.sh            (after sh pc/3DForge/build.sh; on Windows)
# MIT licence (Onyx).
set -e
cd "$(dirname "$0")/../../.."
OUT=$(pwd)/screenshots; EXE=pc/dist/3DForge/3DForge.exe; PART=pc/dist/3DForge/samples/bracket.3df
shot () { name=$1; shift; "$EXE" "$@" --shot "$OUT/$name.png" --size 1280x800; echo "$OUT/$name.png"; }
shot 3dforge-win "$PART"
shot 3dforge-win-box --do "tool:1;move:470,380;click:470,380;move:560,430;click:560,430;move:560,330"
shot 3dforge-win-sketch "$PART" --do "tool:3;cmd:117;tool:18;click:300,300;click:600,450;tool:19;click:450,375;move:500,375"
shot 3dforge-win-cam "$PART" --do "cmd:119;cmd:122;wait:1500"
shot 3dforge-win-resin "$PART" --do "cmd:119;cmd:210;wait:300;cmd:134;cmd:137;wait:800"
shot 3dforge-win-export "$PART" --do "dlg:export"
shot 3dforge-win-loft --do "tool:3;cmd:117;tool:18;click:380,420;click:580,260;cmd:107;tool:3;form:0=30;cmd:117;tool:19;click:480,340;click:520,340;cmd:107;tool:25;cmd:109;look:-50,20;cmd:201;tl:2;wait:300"	# (a rectangle on the ground, a circle 30 above, the loft)
