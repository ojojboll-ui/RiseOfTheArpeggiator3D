#!/bin/sh
# Plays the autopilot and saves screenshots to shots/<name>/, then quits.
# Usage: ./snap.sh name [seconds] [every] [more flags, e.g. --level 2 --slowtest]
cd "$(dirname "$0")"
G="${GODOT:-/c/Users/oloff/Downloads/Godot_v4.7.2-stable_win64.exe/Godot_v4.7.2-stable_win64_console.exe}"
name="$1"
seconds="${2:-12}"
every="${3:-4}"
shift; shift; shift
mkdir -p "shots/$name"
timeout 300 "$G" --path . -- --demo --seconds "$seconds" --every "$every" --shots "$PWD/shots/$name" "$@" </dev/null 2>&1 \
  | grep -v -e "^$" -e "^Godot Engine" -e "^Vulkan" -e "^OpenGL"
true
