#pragma once
#include <android/asset_manager.h>
#include <android/native_window.h>
#include <vector>
#include <string>
#include "common.h"

bool loadImageAsset(AAssetManager* am, const char* name, std::vector<uint8_t>& rgba, int& w, int& h);

class Renderer;
Renderer* rendererCreate(AAssetManager* am, int quality);
void rendererDestroy(Renderer* r);
bool rendererSetSurface(Renderer* r, ANativeWindow* win, int w, int h);  // null win -> destroy swapchain
void rendererDraw(Renderer* r, const float* params, int n);
bool rendererReady(Renderer* r);
