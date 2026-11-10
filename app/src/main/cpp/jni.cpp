#include <jni.h>
#include <android/native_window_jni.h>
#include <android/asset_manager_jni.h>
#include "renderer.h"

extern "C" {
JNIEXPORT jlong JNICALL Java_com_openlivewallpaper_app_NativeRenderer_nCreate(JNIEnv* env, jclass, jobject assets, jint quality) {
    AAssetManager* am = AAssetManager_fromJava(env, assets);
    return (jlong)(intptr_t)rendererCreate(am, quality);
}
JNIEXPORT void JNICALL Java_com_openlivewallpaper_app_NativeRenderer_nDestroy(JNIEnv*, jclass, jlong h) { rendererDestroy((Renderer*)(intptr_t)h); }
JNIEXPORT jboolean JNICALL Java_com_openlivewallpaper_app_NativeRenderer_nSetSurface(JNIEnv* env, jclass, jlong h, jobject surface, jint w, jint hh) {
    ANativeWindow* win = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
    bool ok = rendererSetSurface((Renderer*)(intptr_t)h, win, w, hh);
    if (win) ANativeWindow_release(win);
    return ok;
}
JNIEXPORT void JNICALL Java_com_openlivewallpaper_app_NativeRenderer_nDraw(JNIEnv* env, jclass, jlong h, jfloatArray params) {
    jfloat* p = env->GetFloatArrayElements(params, nullptr);
    rendererDraw((Renderer*)(intptr_t)h, p, env->GetArrayLength(params));
    env->ReleaseFloatArrayElements(params, p, JNI_ABORT);
}
}
