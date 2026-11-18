#!/usr/bin/env bash
# Steam 入口：A=确认 B=取消、60 帧、Deck 图示。发布版本由 current/bundle.json 决定，
# 因此升级发布不需要改这里；重装掌机时把本文件装到 ~/native-recomp/launchers/ 即可。
set -euo pipefail
ulimit -c 0
unset SAVAGEWIND_INPUT SAVAGEWIND_NO_USER_INPUT SAVAGEWIND_MENU_FPS SAVAGEWIND_EVENT_FPS SAVAGEWIND_MOVIE_FPS
unset SAVAGEWIND_LOG_FRAMEPACE LARECOMP_FNPROF
export SAVAGEWIND_PAD_TYPE=deck
export SAVAGEWIND_MENU_CONFIRM=1
exec "$HOME/native-recomp/botw-native/current/run.sh"
