# SPDX-License-Identifier: GPL-3.0-or-later
# ChaOS: where a port's sound goes. Sourced by run.sh; tools/portaudio-check.sh
# runs the same function against the Brick's own ALSA configuration.
#
#   chaos_port_sound SINK ASOUNDRC SYSTEM_ALSA_CONF WORK_DIR
#
# SINK: the headset's PCM, its name in ASOUNDRC (bt-alsa.sh's), when the
# launcher sends the system's sound to one (CHAOS_PORT_SINK: Audio Output,
# a cable and the connected headset decided as for games and Muse,
# src/audioout.h), else empty: the shared mixer, ALSA's default from
# /etc/asound.conf.
#
# How: ALSA reads every file in ALSA_CONFIG_PATH first and alsa.conf's hooks
# after, and those load /etc/asound.conf, whose `default` is the mixer - so a
# `default` in ALSA_CONFIG_PATH never wins (run.sh's "straight to the
# hardware" never took, 2.7.9-test5 to 2.8.0-test4). The headset's is put in
# the hooks themselves, after /etc/asound.conf: the system's alsa.conf with
# one more file in its load list, written to WORK_DIR each time, so it is
# always the system's own. Not ~/.asoundrc, which the hooks also load last:
# a port that sets HOME for itself would lose it.
#
# Says on stdout which it chose, for the port's log, never the headset's name
# or address, and in CHAOS_SOUND (headset or mixer). Sets ALSA_CONFIG_PATH
# for the headset; leaves it as the launcher gave it otherwise.
chaos_port_sound() {
	_sink=$1
	case "$_sink" in
	bt_*) ;;
	*) _sink= ;;
	esac
	# a name of letters, digits and _ only, and defined where ALSA will read
	# it: anything else is no headset to send to
	case "$_sink" in *[!A-Za-z0-9_]*) _sink= ;; esac
	# and its audio stream there already: a headset connected without one
	# opens nothing, and the port would have no sound at all, where the
	# mixer always opens. bluealsa lists each device's PCMs under its own
	# line (launch.sh's bt_a2dp_ready); without bluealsa-aplay, not asked.
	if [ -n "$_sink" ] && command -v bluealsa-aplay > /dev/null 2>&1; then
		_mac=$(echo "${_sink#bt_}" | tr _ :)
		bluealsa-aplay -l 2> /dev/null |
			awk -v m="$_mac" '/^hci/ { on = index($0, m) > 0 } on && /A2DP/ { f = 1 } END { exit !f }' ||
			_sink=
		unset _mac
	fi
	if [ -n "$_sink" ] && grep -q "^pcm\.$_sink {" "$2" 2> /dev/null &&
	   sed "s#^\([[:space:]]*\)\"/etc/asound.conf|||/usr/etc/asound.conf\"\$#&\n\1\"$4/chaos-ports-headset.conf\"#" \
	       "$3" > "$4/chaos-ports-alsa.conf" &&
	   grep -q "\"$4/chaos-ports-headset.conf\"" "$4/chaos-ports-alsa.conf" &&
	   printf 'pcm.!default {\n    type plug\n    slave.pcm "%s"\n}\n' "$_sink" > "$4/chaos-ports-headset.conf"; then
		export ALSA_CONFIG_PATH="$4/chaos-ports-alsa.conf:$2"
		CHAOS_SOUND=headset
		echo "ChaOS: sound through the Bluetooth headset"
	else
		CHAOS_SOUND=mixer
		echo "ChaOS: sound through the shared mixer"
	fi
	unset _sink
}
