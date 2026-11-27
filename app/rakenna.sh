#!/bin/sh
# Build the desktop app on this Mac. Rust toolchain and build output on /Volumes/HOMEROS (internal disk is tight).
#   sh app/rakenna.sh          BusCraft.app
#   sh app/rakenna.sh dmg      BusCraft.app + .dmg
#   sh app/rakenna.sh open     start the last build
set -e
cd "$(dirname "$0")"
H=/Volumes/HOMEROS/rust
export RUSTUP_HOME=$H/rustup CARGO_HOME=$H/cargo CARGO_TARGET_DIR=$H/target/buscraft-app
export PATH=$H/cargo/bin:$HOME/.nvm/versions/node/v22.12.0/bin:/opt/homebrew/bin:$PATH
[ -d node_modules ] || npm install --silent
case "${1:-app}" in
  open) open "$CARGO_TARGET_DIR/release/bundle/macos/BusCraft.app" ;;
  dmg) npx tauri build --bundles app,dmg ;;
  *) npx tauri build --bundles app ;;
esac
