#!/bin/bash
# iolinki Unified Docker Test Runner
#
# Runs code-quality + unit + conformance on the Linux host image.
set -e

echo "============================================"
echo "🐳 iolinki Docker-based Validation Suite"
echo "============================================"

echo -e "\n[1/1] 🐧 Running Linux Host Tests..."
if [[ "$(docker images -q iolinki-test 2> /dev/null)" == "" ]]; then
    echo "   ⚠️ Test image not found. Building..."
    docker build -f Dockerfile.test -t iolinki-test .
else
    echo "   ✅ Test image found. Reusing cached image."
fi
docker run --rm -v "$(pwd)":/workspace iolinki-test bash -c "./check_quality.sh && bash /workspace/.docker_test_entrypoint.sh"

echo -e "\n============================================"
echo "✅ Dockerized Tests Completed Successfully"
echo "============================================"
