#!/usr/bin/env bash
set -euo pipefail
: "${LABWIRED_ROOT:?Set LABWIRED_ROOT to the engine checkout}"
: "${IOLINKI_FIRMWARE:?Set IOLINKI_FIRMWARE to the built ELF}"
mode="${1:?Usage: run.sh g0}"
case "$mode" in
  g0) engine=4ff8cbaaa87418ace5e85be45c220b371f8c5b6f; variable=IOLINKI_G0_ELF; tests=(--test g0_uart_pad --test g0_exti --test g0_reference_firmware) ;;
  *) echo "Unknown target: $mode" >&2; exit 2 ;;
esac
actual="$(git -C "$LABWIRED_ROOT" rev-parse HEAD)"
if [[ "$actual" != "$engine" ]]; then echo "Expected engine $engine, got $actual" >&2; exit 1; fi
firmware="$(realpath "$IOLINKI_FIRMWARE")"
export "$variable=$firmware"
sha256sum "$firmware"
cd "$LABWIRED_ROOT"
cargo test --locked -p labwired-core "${tests[@]}" -- --include-ignored
cargo test --locked -p labwired-core --features event-scheduler "${tests[@]}" -- --include-ignored
if [[ "$mode" == g0 ]]; then
  cargo run --locked -p labwired-cli --bin labwired -- test --script validation/iolinki/g0-startup.yaml --firmware "$firmware"
fi
