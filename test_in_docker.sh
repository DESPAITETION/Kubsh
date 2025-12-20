#!/bin/bash

echo "=== Running kubsh tests in Docker container ==="

DOCKER_IMAGE="tyvik/kubsh_test:master"

echo "1. Pulling Docker image..."
docker pull $DOCKER_IMAGE

echo "2. Full test in Docker (build + test + package)..."
docker run --rm \
    -v $(pwd):/kubsh \
    -w /kubsh \
    $DOCKER_IMAGE \
    bash -c "make build && make test && make package"

echo "=== Docker testing complete ==="
echo "Check if kubsh_1.0.0_amd64.deb was created"