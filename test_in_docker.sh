#!/bin/bash

echo "=== Running kubsh tests in Docker container ==="

DOCKER_IMAGE="tyvik/kubsh_test:master"

echo "1. Pulling Docker image..."
docker pull $DOCKER_IMAGE 2>/dev/null || echo "Using local image"

echo "2. Installing dependencies and running full test..."
docker run --rm \
    -v $(pwd):/kubsh \
    -w /kubsh \
    $DOCKER_IMAGE \
    bash -c "
        echo '=== Updating packages ==='
        apt-get update
        
        echo '=== Installing build tools ==='
        apt-get install -y g++ make dpkg-dev python3-pip 2>/dev/null || apt-get install -y g++ make dpkg-dev python3
        
        echo '=== Installing pytest ==='
        pip3 install pytest 2>/dev/null || echo 'pytest already installed'
        
        echo '=== Building kubsh ==='
        make build || { echo 'Build failed'; exit 1; }
        
        echo '=== Running tests ==='
        make test || { echo 'Tests failed'; exit 1; }
        
        echo '=== Building deb package ==='
        make package || { echo 'Package build failed'; exit 1; }
        
        echo '=== Success! ==='
        ls -la *.deb
    "

echo "=== Docker testing complete ==="
if [ -f "kubsh_1.0.0_amd64.deb" ]; then
    echo "✓ Debian package created: kubsh_1.0.0_amd64.deb"
else
    echo "✗ No deb package found"
fi