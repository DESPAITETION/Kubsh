#!/bin/bash

echo "=== Setting up kubsh on Ubuntu ==="

# Проверяем Docker
if ! command -v docker &> /dev/null; then
    echo "Installing Docker..."
    sudo apt-get update
    sudo apt-get install -y docker.io
    sudo systemctl start docker
    sudo systemctl enable docker
    sudo usermod -aG docker $USER
    echo "✓ Docker installed. Please logout and login again."
else
    echo "✓ Docker already installed"
fi

# Скачиваем тестовый образ
echo "Pulling test Docker image..."
docker pull tyvik/kubsh_test:master

# Устанавливаем инструменты сборки
echo "Installing build tools..."
sudo apt-get install -y build-essential g++ make cmake dpkg-dev lsblk

echo -e "\n=== Setup complete! ==="
echo "Run: make docker-test  # to test your build"