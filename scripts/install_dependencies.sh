#!/bin/bash
# This script will install the dependencies required to run the agent

# General system update
echo "Updating system"
sudo apt-get update -y
sudo apt-get upgrade -y

# Installing system dependencies
echo "Installing system dependencies"
sudo apt-get install -y python3 pipx

pipx install poetry

 # Installing python environment
echo "Installing python environment"
poetry install
