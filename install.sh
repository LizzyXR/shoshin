#!/bin/sh

CONF_FILE="shoshin.conf"
CONF_DIR="$HOME/.config/shoshin"
CONF_DEST="$CONF_DIR/$CONF_FILE"

OVERWRITE=0

for arg in "$@"; do
	case "$arg" in
		--overwrite) OVERWRITE=1 ;;
	esac
done

echo "running bmake..."
if ! bmake; then
	echo "build failed"
	exit 1
fi

if command -v doas >/dev/null 2>&1; then
	ESCALATE="doas"
elif command -v sudo >/dev/null 2>&1; then
	ESCALATE="sudo"
else
	ESCALATE=""
fi

echo "installing..."

if [ "$(id -u)" -eq 0 ]; then
	bmake install
elif [ -n "$ESCALATE" ]; then
	"$ESCALATE" bmake install
else
	echo "need root but no sudo or doas found"
	exit 1
fi

echo "installing config..."

if [ -f "$CONF_DEST" ] && [ "$OVERWRITE" -eq 0 ]; then
	echo "config exists at $CONF_DEST"
	echo "skipping... (use --overwrite to replace)"
else
	cp "$CONF_FILE" "$CONF_DEST"
	echo "installing config to $CONF_DEST"
fi

echo "done!"
