#pragma once
#include <vector>

// Minimal libpng wrapper: load/save 8-bit RGBA images.
bool loadPng(const char* path, std::vector<unsigned char>& rgba, int& width, int& height);
bool savePng(const char* path, const unsigned char* rgba, int width, int height);
