#!/bin/sh
set -eu
cargo test --locked --jobs 2
cargo build --locked --jobs 2
