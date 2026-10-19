@echo off
rem Starts the game with Godot. Set GODOT to your Godot 4.5+ executable, or
rem open the folder in the Godot editor and press F5.
setlocal
if not defined GODOT set "GODOT=C:\Users\oloff\Downloads\Godot_v4.7.2-stable_win64.exe\Godot_v4.7.2-stable_win64.exe"
start "" "%GODOT%" --path "%~dp0" %*
