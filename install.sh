#!/bin/bash

set -e

make
sudo mv keypace /usr/local/bin/
sudo mkdir -p /usr/share/keypace/
mkdir -p ~/.config/keypace/
mkdir -p ~/.local/share/keypace/
cp words.txt ~/.config/keypace/
sudo cp words.data /usr/share/keypace/
