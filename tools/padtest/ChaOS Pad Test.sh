#!/bin/bash
# ChaOS Pad Test: what a port sees of the pad (chaos-padtest.c says what).
# Laid out as PortMaster's ports are, so it runs the way they do.

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source $controlfolder/control.txt
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR="/$directory/ports/chaospadtest"
cd "$GAMEDIR"
echo "padtest: device $DEVICE_NAME ($DEVICE_ARCH), CFW $CFW_NAME, ANALOGSTICKS=$ANALOGSTICKS"
echo "padtest: SDL_GAMECONTROLLERCONFIG=$sdl_controllerconfig"

export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
$GPTOKEYB "chaos-padtest" -c "./padtest.gptk" &
pm_platform_helper "$GAMEDIR/chaos-padtest"
./chaos-padtest ./font.ttf

pm_finish
