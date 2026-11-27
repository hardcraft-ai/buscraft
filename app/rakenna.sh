#!/bin/sh
# Build the desktop app on this Mac. Rust toolchain and build output on /Volumes/HOMEROS (internal disk is tight).
#   sh app/rakenna.sh          BusCraft.app
#   sh app/rakenna.sh dmg      BusCraft.app + .dmg
#   sh app/rakenna.sh open     start the last build
#   sh app/rakenna.sh android  debug APK for arm64 phones, installed with adb if a phone is connected
set -e
cd "$(dirname "$0")"
H=/Volumes/HOMEROS/rust
export RUSTUP_HOME=$H/rustup CARGO_HOME=$H/cargo CARGO_TARGET_DIR=$H/target/buscraft-app
export PATH=$H/cargo/bin:$HOME/.nvm/versions/node/v22.12.0/bin:/opt/homebrew/bin:$PATH
export ANDROID_HOME=$HOME/Library/Android/sdk NDK_HOME=$HOME/Library/Android/sdk/ndk/27.1.12297006
export JAVA_HOME="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
[ -d node_modules ] || npm install --silent
case "${1:-app}" in
  open) open "$CARGO_TARGET_DIR/release/bundle/macos/BusCraft.app" ;;
  dmg) npx tauri build --bundles app,dmg ;;
  android)
    npx tauri android build --apk --target aarch64 --debug
    APK=src-tauri/gen/android/app/build/outputs/apk/universal/debug/app-universal-debug.apk
    "$ANDROID_HOME/platform-tools/adb" get-state >/dev/null 2>&1 && "$ANDROID_HOME/platform-tools/adb" install -r "$APK" ;;
  *) npx tauri build --bundles app ;;
esac
