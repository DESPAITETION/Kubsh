#!/bin/bash

echo "=== Running kubsh tests in Docker container ==="

DOCKER_IMAGE="tyvik/kubsh_test:master"

echo "1. Pulling Docker image..."
docker pull $DOCKER_IMAGE

echo "2. Testing compilation in Docker..."
docker run --rm \
    -v $(pwd):/kubsh \
    -w /kubsh \
    $DOCKER_IMAGE \
    make build

echo "3. Running tests..."
docker run --rm \
    -v $(pwd):/kubsh \
    -w /kubsh \
    $DOCKER_IMAGE \
    make test

echo "4. Building deb package..."
docker run --rm \
    -v $(pwd):/kubsh \
    -w /kubsh \
    $DOCKER_IMAGE \
    make package

echo "=== Docker testing complete ==="
echo "Deb package: kubsh_1.0.0_amd64.deb"