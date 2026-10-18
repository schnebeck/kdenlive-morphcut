#!/bin/bash
# kdenlive-morphcut - install.sh
# Build and install the Morph Cut transition for Kdenlive.
#
#   Copyright (C) 2026 Thorsten Schnebeck <thorsten.schnebeck@gmx.net>
#   Produced by Thorsten Schnebeck - the idea, the decisions, the testing.
#   Written by Anthropic Claude Opus 5.5 - AI generated content.
#
#   Free software under the GNU General Public License, version 3 or later.
#   There is no warranty, to the extent permitted by law. The full text is in
#   LICENSES/GPL-3.0-or-later.txt.
#
# SPDX-FileCopyrightText: (C) 2026 Thorsten Schnebeck <thorsten.schnebeck@gmx.net>
# SPDX-FileContributor: Anthropic Claude Opus 5.5 (AI generated content)
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   ./install.sh            install for the current user (no root needed)
#   ./install.sh --system   install into the system MLT directories (sudo)
#   ./install.sh --uninstall
#
# Environment: NCNN_DIR=/path/to/lib/cmake/ncnn to use a prebuilt ncnn,
#              otherwise ncnn is downloaded and built (needs git + network).
set -euo pipefail

SRC="$(cd "$(dirname "$0")" && pwd)"
BUILD="$SRC/build"
MODE="${1:---user}"

DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
USER_LIB="$HOME/.local/lib/morphcut"          # module + MLT repository overlay
USER_REPO="$USER_LIB/mlt-7"
USER_DATA="$DATA_HOME/morphcut"
KDENLIVE_XML_DIR="$DATA_HOME/kdenlive/transitions"
PLASMA_ENV="$HOME/.config/plasma-workspace/env/morphcut.sh"
DESKTOP="$DATA_HOME/applications/org.kde.kdenlive.desktop"

system_mlt_dir() {
    local lib
    lib=$(ldconfig -p | awk '/libmlt-7\.so\.7 /{print $NF; exit}')
    [ -n "$lib" ] || { echo "libmlt-7 not found - is Kdenlive installed?" >&2; exit 1; }
    echo "$(dirname "$(readlink -f "$lib")")/mlt-7"
}

build() {
    local extra=("$@")
    if [ -n "${NCNN_DIR:-}" ]; then
        extra+=("-Dncnn_DIR=$NCNN_DIR")
    fi
    cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DMORPHCUT_BUILD_TOOLS=OFF "${extra[@]}"
    cmake --build "$BUILD" -j"$(nproc)"
}

install_kdenlive_xml() {
    mkdir -p "$KDENLIVE_XML_DIR"
    install -m644 "$SRC/kdenlive/morphcut.xml" "$KDENLIVE_XML_DIR/morphcut.xml"
}

# The overlay mirrors the system MLT modules and adds ours; Kdenlive is
# started with MLT_REPOSITORY pointing to it. It is refreshed on every start,
# so MLT updates (new or removed modules) are picked up automatically.
write_user_launchers() {
    local sysdir="$1"
    cat > "$USER_LIB/refresh-overlay.sh" <<EOF
#!/bin/sh
# Rebuild the MLT module overlay for the Morph Cut transition.
repo="$USER_REPO"
mkdir -p "\$repo"
find "\$repo" -maxdepth 1 -type l -delete
for so in "$sysdir"/*.so; do
    [ -e "\$so" ] && ln -sf "\$so" "\$repo/"
done
ln -sf "$USER_LIB/libmltmorphcut.so" "\$repo/libmltmorphcut.so"

# Kdenlive translates compositions only through its own "kdenlive" catalog,
# so a copy of it with our strings added goes to the user's locale directory
# (searched first). It is rebuilt when Kdenlive or our translation changes;
# Kdenlive's own translations take precedence for strings it already has.
command -v msgfmt >/dev/null && command -v msgunfmt >/dev/null && command -v msgcat >/dev/null || exit 0
for po in "$USER_DATA"/po/*.po; do
    [ -e "\$po" ] || continue
    lang=\$(basename "\$po" .po)
    sys="/usr/share/locale/\$lang/LC_MESSAGES/kdenlive.mo"
    dst="$DATA_HOME/locale/\$lang/LC_MESSAGES/kdenlive.mo"
    [ -f "\$dst" ] && [ "\$dst" -nt "\$po" ] && { [ ! -f "\$sys" ] || [ "\$dst" -nt "\$sys" ]; } && continue
    mkdir -p "\$(dirname "\$dst")"
    tmp="\$dst.tmp.\$\$"
    if [ -f "\$sys" ]; then
        msgunfmt "\$sys" | msgcat --use-first - "\$po" | msgfmt -o "\$tmp" - 2>/dev/null
    else
        msgfmt -o "\$tmp" "\$po" 2>/dev/null
    fi && mv -f "\$tmp" "\$dst" || rm -f "\$tmp"
    echo "\$dst" >> "$USER_LIB/catalogs.list"
done
sort -u -o "$USER_LIB/catalogs.list" "$USER_LIB/catalogs.list" 2>/dev/null
exit 0
EOF
    chmod +x "$USER_LIB/refresh-overlay.sh"

    cat > "$USER_LIB/kdenlive" <<EOF
#!/bin/sh
# Start Kdenlive with the Morph Cut transition available.
"$USER_LIB/refresh-overlay.sh"
MLT_REPOSITORY="$USER_REPO" exec /usr/bin/kdenlive "\$@"
EOF
    chmod +x "$USER_LIB/kdenlive"

    # Session-wide (after the next login): also covers melt and file associations.
    mkdir -p "$(dirname "$PLASMA_ENV")"
    cat > "$PLASMA_ENV" <<EOF
# Morph Cut transition for Kdenlive (kdenlive-morphcut)
"$USER_LIB/refresh-overlay.sh"
export MLT_REPOSITORY="$USER_REPO"
EOF

    # Menu entry override: works immediately, without a new login.
    if [ -f /usr/share/applications/org.kde.kdenlive.desktop ]; then
        mkdir -p "$(dirname "$DESKTOP")"
        sed "s|^Exec=kdenlive|Exec=$USER_LIB/kdenlive|" /usr/share/applications/org.kde.kdenlive.desktop > "$DESKTOP"
        update-desktop-database "$(dirname "$DESKTOP")" 2>/dev/null || true
    fi
}

case "$MODE" in
--user)
    SYSDIR=$(system_mlt_dir)
    build -DMORPHCUT_MODULE_DIR="$USER_LIB" -DMORPHCUT_DATA_DIR="$USER_DATA"
    cmake --install "$BUILD"
    write_user_launchers "$SYSDIR"
    "$USER_LIB/refresh-overlay.sh"
    install_kdenlive_xml
    echo
    echo "Installed for $USER."
    echo "Start Kdenlive from the application menu (or $USER_LIB/kdenlive)."
    echo "After the next login it also works when started as plain 'kdenlive'."
    ;;
--system)
    build
    sudo cmake --install "$BUILD"
    install_kdenlive_xml
    echo
    echo "Installed system-wide. Restart Kdenlive."
    ;;
--uninstall)
    # The running session may still export MLT_REPOSITORY: keep the overlay
    # (links to the system modules only) until the next login.
    rm -f "$USER_LIB/libmltmorphcut.so" "$USER_REPO/libmltmorphcut.so" "$USER_LIB/kdenlive" \
          "$USER_LIB/refresh-overlay.sh" "$PLASMA_ENV" "$KDENLIVE_XML_DIR/morphcut.xml"
    if [ -f "$USER_LIB/catalogs.list" ]; then
        xargs -r rm -f < "$USER_LIB/catalogs.list"
        rm -f "$USER_LIB/catalogs.list"
    fi
    rm -rf "$USER_DATA"
    if [ -f "$DESKTOP" ] && grep -q "$USER_LIB" "$DESKTOP"; then rm -f "$DESKTOP"; fi
    SYSDIR=$(system_mlt_dir)
    if [ -e "$SYSDIR/libmltmorphcut.so" ]; then
        echo "Removing system files (sudo)..."
        sudo rm -f "$SYSDIR/libmltmorphcut.so"
        sudo rm -rf /usr/share/mlt-7/morphcut
    fi
    echo "Uninstalled. After the next login you may delete $USER_LIB."
    ;;
*)
    sed -n '/^#   \.\/install\.sh /,/^#  *otherwise/p' "$0"
    exit 1
    ;;
esac
